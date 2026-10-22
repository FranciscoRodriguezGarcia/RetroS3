#include "launcher.h"
#include "ui_media.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "display.h"
#include "input.h"
#include "emulator_manager.h"
#include "emulator_diag.h"
#include "status_led.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LAUNCHER";

static launcher_state_t state;
static ui_animation_t *console_media[SYSTEM_MEGADRIVE + 1];
static bool console_attempted[SYSTEM_MEGADRIVE + 1];
static ui_animation_t *game_cover;
static char game_cover_path[ROM_PATH_MAX];
static bool error_visible;
#define TITLE_COLUMNS 24
static struct {
    char text[ROM_NAME_MAX];
    int length, offset, direction, y;
    int64_t next_us;
} title_scroll;
#define UI_COLOR(c) ((uint16_t)(((c) << 8) | ((c) >> 8)))
#define UI_BG UI_COLOR(0x1083)
#define UI_ACCENT UI_COLOR(0x4e99)
static void launcher_release_media(void)
{
    ui_animation_free(game_cover); game_cover=NULL; game_cover_path[0]=0;
    for (int i=0; i<=SYSTEM_MEGADRIVE; i++) {
        ui_animation_free(console_media[i]); console_media[i]=NULL; console_attempted[i]=false;
    }
}
static ui_animation_t *launcher_console_media(void)
{
    if (!state.systems.count) return NULL;
    emulator_system_t system=state.systems.entries[state.selected_system_index].system;
    if (system<=SYSTEM_UNKNOWN || system>SYSTEM_MEGADRIVE) return NULL;
    return console_media[system];
}

static void launcher_render(void);
static void launcher_render_systems(void);
static void launcher_render_roms(void);
static void launcher_task(void *arg);
static void launcher_render_error(const rom_entry_t *rom)
{
    const uint16_t bg = UI_COLOR(0x1083);
    const uint16_t red = UI_COLOR(0xf800);
    display_fill_rect(0, 0, 320, 240, bg);
    display_fill_rect(0, 0, 320, 32, red);
    display_draw_text(30, 12, "ERROR AL INICIAR ROM", 0xffff, red);

    char line[48];
    snprintf(line, sizeof(line), "CONSOLA: %s", rom_manager_system_name(rom->system));
    display_draw_text(12, 51, line, 0xffff, bg);
    display_draw_text(12, 76, emulator_diag_label(), red, bg);
    snprintf(line, sizeof(line), "%.46s", emulator_diag_detail());
    display_draw_text(12, 103, line, 0xffff, bg);

    snprintf(line, sizeof(line), "ROM: %.39s", rom->name);
    display_draw_text(12, 130, line, 0xffff, bg);
    snprintf(line, sizeof(line), "PSRAM LIBRE: %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    display_draw_text(12, 157, line, 0xffff, bg);
    display_draw_text(52, 211, "A: VOLVER A ROMS", 0xffff, bg);
}
static void launcher_title_tick(void)
{
    if (title_scroll.length<=TITLE_COLUMNS || esp_timer_get_time()<title_scroll.next_us) return;
    title_scroll.offset+=title_scroll.direction;
    bool edge=title_scroll.offset==0 || title_scroll.offset==title_scroll.length-TITLE_COLUMNS;
    if (edge) title_scroll.direction=-title_scroll.direction;
    title_scroll.next_us=esp_timer_get_time()+(edge ? 1000000 : 150000);
    char window[TITLE_COLUMNS+1];
    memcpy(window,title_scroll.text+title_scroll.offset,TITLE_COLUMNS);
    window[TITLE_COLUMNS]=0;
    display_fill_rect(20,title_scroll.y,TITLE_COLUMNS*6,7,UI_ACCENT);
    display_draw_text(20,title_scroll.y,window,UI_COLOR(0x0841),UI_ACCENT);
}
//-----------------------------
static const rom_entry_t *launcher_get_selected_rom(void)
{
    if (state.systems.count == 0)
    {
        return NULL;
    }

    emulator_system_t system =
        state.systems
            .entries[state.selected_system_index]
            .system;

    int visible_index = 0;

    for (size_t i = 0;
         i < state.roms->count;
         i++)
    {
        const rom_entry_t *rom =
            &state.roms->entries[i];

        if (rom->system != system)
        {
            continue;
        }

        if (visible_index ==
            state.selected_rom_index)
        {
            return rom;
        }

        visible_index++;
    }

    return NULL;
}
//-------------------------------------
static void launcher_task(void *arg)
{
    ui_play_startup();
    launcher_render();
    status_led_set_mode(STATUS_LED_OFF);

    input_state_t previous_state = input_get_state();

    while (true)
    {
        /* GB/GBC starts asynchronously. Only the active emulator may consume
         * MENU or draw on the TFT until it exits (currently by reboot). */
        if (emulator_manager_is_running())
        {
            previous_state = input_get_state();
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (input_consume_menu_request())
{
    if (state.screen == LAUNCHER_SCREEN_ROMS)
    {
        state.screen = LAUNCHER_SCREEN_SYSTEMS;
        state.selected_rom_index = 0;
        state.scroll_offset = 0;

        launcher_render();
    }
}
        input_state_t current_state =
            input_get_state();

        /*
         * Detectamos sólo botones que acaban
         * de pasar de suelto -> presionado.
         */
        input_state_t pressed =
            current_state &
            ~previous_state;

        if (error_visible) {
            if (pressed & (INPUT_BUTTON_A | INPUT_BUTTON_B)) {
                error_visible = false;
                launcher_render();
            }
            previous_state = current_state;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if ((pressed & INPUT_BUTTON_B) && state.screen == LAUNCHER_SCREEN_ROMS) {
            state.screen = LAUNCHER_SCREEN_SYSTEMS;
            launcher_render();
        }
        if (state.screen == LAUNCHER_SCREEN_SYSTEMS && state.systems.count &&
            (pressed & (INPUT_BUTTON_LEFT | INPUT_BUTTON_RIGHT))) {
            int direction = (pressed & INPUT_BUTTON_RIGHT) ? 1 : -1;
            state.selected_system_index = (state.selected_system_index + direction + (int)state.systems.count) % (int)state.systems.count;
            launcher_render();
        }
        if (pressed & INPUT_BUTTON_UP)
        {
            if (state.screen ==
                LAUNCHER_SCREEN_SYSTEMS)
            {
                if (state.selected_system_index > 0)
                {
                    state.selected_system_index--;
                    launcher_render();
                }
            }
            else if (state.screen ==
                     LAUNCHER_SCREEN_ROMS)
            {
                if (state.selected_rom_index > 0)
                {
                    state.selected_rom_index--;
                    launcher_render();
                }
            }
        }

        if (pressed & INPUT_BUTTON_DOWN)
        {
            if (state.screen ==
                LAUNCHER_SCREEN_SYSTEMS)
            {
                if (state.selected_system_index <
                    (int)state.systems.count - 1)
                {
                    state.selected_system_index++;
                    launcher_render();
                }
            }
            else if (state.screen ==
                     LAUNCHER_SCREEN_ROMS)
            {
                system_entry_t *system =
                    &state.systems.entries[
                        state.selected_system_index
                    ];

                if (state.selected_rom_index <
                    (int)system->rom_count - 1)
                {
                    state.selected_rom_index++;
                    launcher_render();
                }
            }
        }

        if ((pressed & INPUT_BUTTON_A) && state.systems.count)
        {
            if (state.screen ==
                LAUNCHER_SCREEN_SYSTEMS)
            {
                state.screen =
                    LAUNCHER_SCREEN_ROMS;

                state.selected_rom_index = 0;
                state.scroll_offset = 0;

                launcher_render();
            }
            else if (state.screen ==
                     LAUNCHER_SCREEN_ROMS)
            {
                const rom_entry_t *rom =
                    launcher_get_selected_rom();

                if (rom != NULL)
                {
                    ESP_LOGI(
                        TAG,
                        "Lanzando: %s",
                        rom->path
                    );

                    launcher_release_media();
                    display_clear();

                   if (!emulator_manager_start(rom))
                    {
                        error_visible = true;
                        launcher_render_error(rom);
                    }
                }
            }
        }

        if (state.screen == LAUNCHER_SCREEN_SYSTEMS && !emulator_manager_is_running()) {
            ui_animation_t *animation=launcher_console_media();
            if (animation) ui_animation_tick(animation,(320-ui_animation_width(animation))/2,42);
        }
        if (state.screen == LAUNCHER_SCREEN_ROMS && !emulator_manager_is_running()) launcher_title_tick();
        previous_state =
            current_state;

        vTaskDelay(
            pdMS_TO_TICKS(20)
        );
    }
}
//-----------------------------------------
bool launcher_start(
    const rom_list_t *roms
)
{
    if (roms == NULL)
    {
        ESP_LOGE(
            TAG,
            "Lista de ROMs nula"
        );

        return false;
    }

    memset(
        &state,
        0,
        sizeof(state)
    );

    state.screen =
        LAUNCHER_SCREEN_SYSTEMS;

    state.roms = roms;

    state.selected_system_index = 0;
    state.selected_rom_index = 0;
    state.scroll_offset = 0;

    if (!rom_manager_get_systems(
            roms,
            &state.systems))
    {
        ESP_LOGE(
            TAG,
            "No pude construir lista de sistemas"
        );

        return false;
    }

    ESP_LOGI(
        TAG,
        "Launcher: %u sistema(s)",
        (unsigned)state.systems.count
    );

    for (size_t i = 0;
         i < state.systems.count;
         i++)
    {
        ESP_LOGI(
            TAG,
            "[%u] %s | %u ROM(s)",
            (unsigned)i,
            state.systems.entries[i].display_name,
            (unsigned)state.systems.entries[i].rom_count
        );
    }

    BaseType_t result =
        xTaskCreatePinnedToCore(
            launcher_task,
            "launcher",
            6144,
            NULL,
            3,
            NULL,
            1
        );

    if (result != pdPASS)
    {
        ESP_LOGE(
            TAG,
            "No pude crear launcher_task"
        );

        return false;
    }

    ESP_LOGI(
        TAG,
        "Launcher task iniciada"
    );

    return true;
}
//---------------------------------------
static void launcher_render(void)
{
    display_fill_rect(0,0,320,240,UI_BG);
    display_fill_rect(0,0,320,28,UI_COLOR(0x0841));
    display_draw_text(16,10,"FRANTASTIC",0xffff,UI_COLOR(0x0841));
    display_draw_text(226,10,"BIBLIOTECA",UI_ACCENT,UI_COLOR(0x0841));
    if (state.screen == LAUNCHER_SCREEN_SYSTEMS) launcher_render_systems();
    else launcher_render_roms();
}

static void launcher_render_systems(void)
{
    if (!state.systems.count) {
        display_draw_text(50,100,"NO HAY CONSOLAS EN LA SD",0xffff,UI_BG);
        display_draw_text(30,124,"COPIA EL CONTENIDO DE FILESYS",UI_ACCENT,UI_BG);
        return;
    }
    system_entry_t *selected=&state.systems.entries[state.selected_system_index];
    emulator_system_t system=selected->system;
    if (system>SYSTEM_UNKNOWN && system<=SYSTEM_MEGADRIVE && !console_attempted[system]) {
        console_attempted[system]=true;
        status_led_set_mode(STATUS_LED_BLINK_FAST);
        display_draw_text(112,108,"CARGANDO...",UI_ACCENT,UI_BG);
        const char *folder=system==SYSTEM_MEGADRIVE ? "MegaDrive" : system==SYSTEM_NES ? "NES" : system==SYSTEM_GAMEBOY_COLOR ? "GameBoyColor" : "GameBoy";
        char path[128];
        snprintf(path,sizeof(path),"/sdcard/%s/console.r53a",folder);
        console_media[system]=ui_animation_load(path);
        if (!console_media[system] && system==SYSTEM_GAMEBOY_COLOR)
            console_media[system]=ui_animation_load("/sdcard/GameBoy/console.r53a");
        display_fill_rect(64,42,192,144,UI_BG);
        status_led_set_mode(STATUS_LED_OFF);
    }
    ui_animation_t *animation=launcher_console_media();
    if (animation) {
        ui_animation_restart(animation);
        ui_animation_tick(animation,(320-ui_animation_width(animation))/2,42);
    } else {
        display_fill_rect(88,58,144,112,UI_COLOR(0x2124));
        display_draw_text(112,106,selected->display_name,0xffff,UI_COLOR(0x2124));
    }
    display_draw_text(16,108,"<",UI_ACCENT,UI_BG);
    display_draw_text(298,108,">",UI_ACCENT,UI_BG);
    char label[64];
    snprintf(label,sizeof(label),"%s  /  %u JUEGOS",selected->display_name,(unsigned)selected->rom_count);
    display_draw_text((320-(int)strlen(label)*6)/2,196,label,0xffff,UI_BG);
}

static void launcher_render_roms(void)
{
    memset(&title_scroll,0,sizeof(title_scroll));
    emulator_system_t system=state.systems.entries[state.selected_system_index].system;
    display_draw_text(16,38,rom_manager_system_name(system),UI_ACCENT,UI_BG);
    if (!state.systems.entries[state.selected_system_index].rom_count) {
        display_draw_text(32,100,"ESTA CONSOLA NO TIENE JUEGOS",0xffff,UI_BG);
        display_draw_text(32,124,"AGREGA ROMS A SU CARPETA",UI_ACCENT,UI_BG);
        display_draw_text(16,230,"B / MENU VOLVER",UI_ACCENT,UI_BG);
        return;
    }
    const int rows=8;
    if (state.selected_rom_index<state.scroll_offset) state.scroll_offset=state.selected_rom_index;
    if (state.selected_rom_index>=state.scroll_offset+rows) state.scroll_offset=state.selected_rom_index-rows+1;
    int index=0;
    for (size_t i=0;i<state.roms->count;i++) {
        const rom_entry_t *rom=&state.roms->entries[i];
        if (rom->system!=system) continue;
        int row=index-state.scroll_offset;
        bool selected=index==state.selected_rom_index;
        index++;
        if (row<0 || row>=rows) continue;
        int y=62+row*18;
        uint16_t background=selected ? UI_ACCENT : UI_BG;
        if (selected) display_fill_rect(12,y-4,158,17,background);
        char title[ROM_NAME_MAX];
        snprintf(title,sizeof(title),"%s",rom->name);
        char *ext=strrchr(title,'.'); if (ext) *ext=0;
        if (selected) {
            snprintf(title_scroll.text,sizeof(title_scroll.text),"%s",title);
            title_scroll.length=strlen(title);
            title_scroll.direction=1;
            title_scroll.y=y;
            title_scroll.next_us=esp_timer_get_time()+1000000;
        }
        title[TITLE_COLUMNS]=0;
        display_draw_text(20,y,title,selected ? UI_COLOR(0x0841) : 0xffff,background);
    }
    const rom_entry_t *rom=launcher_get_selected_rom();
    if (rom) {
        char path[ROM_PATH_MAX];
        const char *slash=strrchr(rom->path,'/');
        int n=slash ? snprintf(path,sizeof(path),"%.*s/cover.r53a",(int)(slash-rom->path),rom->path) : -1;
        if (n>0 && n<sizeof(path)) {
            if (strcmp(path,game_cover_path)!=0) {
                ui_animation_free(game_cover); game_cover=NULL;
                snprintf(game_cover_path,sizeof(game_cover_path),"%s",path);
                game_cover=ui_animation_load(path);
            }
            if (game_cover && ui_animation_width(game_cover)<=112 && ui_animation_height(game_cover)<=84) {
                ui_animation_restart(game_cover);
                ui_animation_tick(game_cover,184+(112-ui_animation_width(game_cover))/2,68);
            } else {
                display_fill_rect(184,68,112,84,UI_COLOR(0x2124));
                display_draw_text(200,106,"SIN PORTADA",UI_ACCENT,UI_COLOR(0x2124));
            }
        }
    }
    title_scroll.next_us=esp_timer_get_time()+1000000;
    display_draw_text(16,230,"A JUGAR    B / MENU VOLVER",UI_ACCENT,UI_BG);
}
