package com.retro.retrocontroller
import androidx.compose.foundation.Image
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.layout.ContentScale
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager
import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.retro.retrocontroller.ui.theme.RetroControllerTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext

private val BackgroundColor = Color(0xFF202126)
private val DirectionColor = Color(0xFF45464D)
private val ActionColor = Color(0xFFB71C36)
private const val USB_PERMISSION_ACTION = "com.retro.retrocontroller.USB_PERMISSION"

enum class NesButton(val bit: Int) {
    A(0), B(1), SELECT(2), START(3), UP(4), DOWN(5), LEFT(6), RIGHT(7)
}

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        setContent {
            RetroControllerTheme {
                NESControllerScreen()
            }
        }
    }
}

class RetroUsbController(private val manager: UsbManager) {
    private var connection: UsbDeviceConnection? = null
    private var dataInterface: UsbInterface? = null
    private var endpointOut: UsbEndpoint? = null
    private var audio: UsbAudio? = null

    fun connect(): Boolean {
        disconnect()
        val device = manager.deviceList.values.firstOrNull {
            it.vendorId == 0x303A && it.productId == 0x1001
        } ?: return false
        if (!manager.hasPermission(device)) return false
        val intf = (0 until device.interfaceCount).map { device.getInterface(it) }
            .firstOrNull { it.interfaceClass == UsbConstants.USB_CLASS_CDC_DATA }
            ?: return false
        val ep = (0 until intf.endpointCount).map { intf.getEndpoint(it) }
            .firstOrNull {
                it.direction == UsbConstants.USB_DIR_OUT &&
                        it.type == UsbConstants.USB_ENDPOINT_XFER_BULK
            } ?: return false
        val input = (0 until intf.endpointCount).map { intf.getEndpoint(it) }
            .firstOrNull { it.direction == UsbConstants.USB_DIR_IN && it.type == UsbConstants.USB_ENDPOINT_XFER_BULK }
            ?: return false
        val conn = manager.openDevice(device) ?: return false
        if (!conn.claimInterface(intf, true)) {
            conn.close()
            return false
        }
        Log.i("RetroAudio", "CDC interface=${intf.id} IN=0x${input.address.toString(16)} max=${input.maxPacketSize} OUT=0x${ep.address.toString(16)} max=${ep.maxPacketSize}")
        connection = conn
        dataInterface = intf
        endpointOut = ep
        audio = UsbAudio(conn, input).also { it.start() }
        return true
    }

    fun audioStatus(): String = audio?.summary ?: "AUDIO: WAIT"

    fun send(mask: Int): Boolean {
        if (audio?.isRunning() != true) return false
        val conn = connection ?: return false
        val ep = endpointOut ?: return false
        val bytes = UsbAudio.packet(4, mask)
        if (conn.bulkTransfer(ep, bytes, bytes.size, 100) != bytes.size) return false
        val heartbeat = UsbAudio.heartbeat()
        return conn.bulkTransfer(ep, heartbeat, heartbeat.size, 100) == heartbeat.size
    }
    fun sendMenu(): Boolean {
        val conn = connection ?: return false
        val ep = endpointOut ?: return false

        val bytes = UsbAudio.packet(5, 1)

        return conn.bulkTransfer(
            ep,
            bytes,
            bytes.size,
            100
        ) == bytes.size
    }
    fun disconnect() {
        audio?.stop()
        audio = null
        val conn = connection
        if (conn != null) {
            dataInterface?.let { conn.releaseInterface(it) }
            conn.close()
        }
        connection = null
        dataInterface = null
        endpointOut = null
    }
}

@Composable
fun NESControllerScreen() {
    val context = LocalContext.current
    val manager = remember(context) {
        context.getSystemService(Context.USB_SERVICE) as UsbManager
    }
    val controller = remember(manager) { RetroUsbController(manager) }
    val pressed = remember { mutableStateMapOf<NesButton, Boolean>() }
    fun updateButton(button: NesButton, down: Boolean) { pressed[button] = down }

    val buttonState = NesButton.entries.fold(0) { mask, button ->
        if (pressed[button] == true) mask or (1 shl button.bit) else mask
    }
    val currentButtonState = rememberUpdatedState(buttonState)
    var menuPressed by remember { mutableStateOf(false) }
    var menuRequestPending by remember {
        mutableStateOf(false)
    }
    var usbConnected by remember { mutableStateOf(false) }
    var usbStatus by remember { mutableStateOf("USB: NOT CONNECTED") }

    LaunchedEffect(manager, controller) {
        var permissionRequestedFor: String? = null
        try {
            while (isActive) {
                val device = manager.deviceList.values.firstOrNull {
                    it.vendorId == 0x303A && it.productId == 0x1001
                }
                if (device == null) {
                    permissionRequestedFor = null
                    usbConnected = false
                    usbStatus = "USB: NOT CONNECTED"
                    withContext(Dispatchers.IO) { controller.disconnect() }
                    delay(2000)
                    continue
                }
                if (!manager.hasPermission(device)) {
                    usbConnected = false
                    usbStatus = "USB: PERMISSION"
                    if (permissionRequestedFor != device.deviceName) {
                        permissionRequestedFor = device.deviceName
                        val permissionIntent = PendingIntent.getBroadcast(
                            context, 0,
                            Intent(USB_PERMISSION_ACTION).setPackage(context.packageName),
                            PendingIntent.FLAG_MUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
                        )
                        manager.requestPermission(device, permissionIntent)
                    }
                    delay(2000)
                    continue
                }
                if (!usbConnected) {
                    usbConnected = withContext(Dispatchers.IO) {
                        runCatching { controller.connect() }.getOrDefault(false)
                    }
                    usbStatus = if (usbConnected) "USB: CONNECTED" else "USB: ERROR"
                }
                if (usbConnected) {
                    val sendMenu = menuRequestPending

                    val ok = withContext(Dispatchers.IO) {
                        runCatching {
                            val buttonsOk = controller.send(currentButtonState.value)
                            val menuOk = if (sendMenu && buttonsOk) {
                                controller.sendMenu()
                            } else {
                                true
                            }
                            buttonsOk && menuOk
                        }.getOrDefault(false)
                    }

                    if (ok) usbStatus = "USB OK | ${controller.audioStatus()}"

                    if (ok && sendMenu) {
                        menuRequestPending = false
                    }

                    if (!ok) {
                        withContext(Dispatchers.IO) { runCatching { controller.disconnect() } }
                        usbConnected = false
                        usbStatus = "USB: ERROR"
                    }
                }
                delay(if (usbConnected) 50 else 2000)
            }
        } finally {
            withContext(kotlinx.coroutines.NonCancellable + Dispatchers.IO) {
                controller.send(0)
                controller.disconnect()
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(BackgroundColor)
            .padding(
                horizontal = 40.dp,
                vertical = 16.dp
            ),
        verticalArrangement = Arrangement.SpaceBetween
    ) {

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = "RETRO ? USB AUDIO TEST",
                color = Color.LightGray,
                fontWeight = FontWeight.Bold,
                fontSize = 18.sp
            )

            Text(
                text = usbStatus,
                color = if (usbConnected) Color.Green else Color.Gray,
                fontSize = 12.sp
            )
        }

        Row(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .padding(horizontal = 40.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {

            // CRUCETA
            Column(
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                DirectionButton(
                    "▲",
                    pressed[NesButton.UP] == true
                ) {
                    updateButton(NesButton.UP, it)
                }

                Row {
                    DirectionButton(
                        "◀",
                        pressed[NesButton.LEFT] == true
                    ) {
                        updateButton(NesButton.LEFT, it)
                    }

                    Box(
                        modifier = Modifier.size(80.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            "+",
                            color = Color.Gray,
                            fontSize = 45.sp
                        )
                    }

                    DirectionButton(
                        "▶",
                        pressed[NesButton.RIGHT] == true
                    ) {
                        updateButton(NesButton.RIGHT, it)
                    }
                }

                DirectionButton(
                    "▼",
                    pressed[NesButton.DOWN] == true
                ) {
                    updateButton(NesButton.DOWN, it)
                }
            }

            // MENU (placeholder local; todavía no se transmite al ESP32)
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                GameButton(
                    label = "Menú",
                    isPressed = menuPressed,
                    color = Color.DarkGray,
                    touchWidth = 90.dp,
                    touchHeight = 58.dp,
                    visualWidth = 74.dp,
                    visualHeight = 54.dp,
                    circular = true,
                    iconRes = R.drawable.icon,
                    onPressed = { down ->
                        menuPressed = down

                        if (down) {
                            menuRequestPending = true
                        }
                    }
                )
                Spacer(Modifier.height(10.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    SmallButton("SELECT", pressed[NesButton.SELECT] == true) {
                        updateButton(NesButton.SELECT, it)
                    }
                    SmallButton("START", pressed[NesButton.START] == true) {
                        updateButton(NesButton.START, it)
                    }
                }
            }

            // BOTONES B / A
            Row(
                verticalAlignment = Alignment.CenterVertically
            ) {
                ActionButton(
                    "B",
                    pressed[NesButton.B] == true
                ) {
                    updateButton(NesButton.B, it)
                }

                ActionButton(
                    "A",
                    pressed[NesButton.A] == true
                ) {
                    updateButton(NesButton.A, it)
                }
            }
        }

    }
}

@Composable
fun DirectionButton(
    label: String,
    isPressed: Boolean,
    onPressed: (Boolean) -> Unit
) {
    GameButton(
        label = label,
        isPressed = isPressed,
        color = DirectionColor,
        touchWidth = 80.dp,
        touchHeight = 80.dp,
        visualWidth = 64.dp,
        visualHeight = 64.dp,
        onPressed = onPressed

    )
}

@Composable
fun ActionButton(
    label: String,
    isPressed: Boolean,
    onPressed: (Boolean) -> Unit
) {
    GameButton(
        label = label,
        isPressed = isPressed,
        color = ActionColor,
        touchWidth = 104.dp,
        touchHeight = 104.dp,
        visualWidth = 76.dp,
        visualHeight = 76.dp,
        circular = true,
        onPressed = onPressed
    )
}

@Composable
fun SmallButton(
    label: String,
    isPressed: Boolean,
    onPressed: (Boolean) -> Unit
) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        GameButton(
            label = "",
            isPressed = isPressed,
            color = Color.DarkGray,
            touchWidth = 90.dp,
            touchHeight = 52.dp,
            visualWidth = 60.dp,
            visualHeight = 28.dp,
            circular = true,
            onPressed = onPressed
        )
        Text(text = label, color = Color.LightGray, fontSize = 18.sp)
    }
}

/** La superficie que captura el toque es toda la celda exterior.
 * El botón visual se dibuja centrado y no intercepta eventos.
 * Las celdas hermanas no se solapan.
 */
@Composable
fun GameButton(
    label: String,
    isPressed: Boolean,
    color: Color,
    touchWidth: androidx.compose.ui.unit.Dp,
    touchHeight: androidx.compose.ui.unit.Dp,
    visualWidth: androidx.compose.ui.unit.Dp,
    visualHeight: androidx.compose.ui.unit.Dp,
    circular: Boolean = false,
    onPressed: (Boolean) -> Unit,
    iconRes: Int? = null
) {
    val shape = if (circular) CircleShape else RoundedCornerShape(8.dp)
    Box(
        modifier = Modifier
            .size(touchWidth, touchHeight)
            .pointerInput(onPressed) {
                detectTapGestures(onPress = {
                    onPressed(true)
                    try { tryAwaitRelease() } finally { onPressed(false) }
                })
            },
        contentAlignment = Alignment.Center
    ) {
        Box(
            modifier = Modifier
                .size(visualWidth, visualHeight)
                .background(if (isPressed) color.copy(alpha = 0.55f) else color, shape)
                .border(2.dp, if (isPressed) Color.White else Color.Transparent, shape),
            contentAlignment = Alignment.Center
        ) {
            if (iconRes != null) {
                Image(
                    painter = painterResource(id = iconRes),
                    contentDescription = label,
                    modifier = Modifier.size(70.dp),
                    contentScale = ContentScale.Fit
                )
            } else {
                Text(
                    text = label,
                    color = Color.White,
                    fontSize = 23.sp,
                    fontWeight = FontWeight.Bold
                )
            }
        }
    }
}

