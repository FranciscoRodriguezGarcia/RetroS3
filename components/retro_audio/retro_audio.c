#include "retro_audio.h"
#include <string.h>
#include <stdatomic.h>
#include <math.h>
#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "driver/usb_serial_jtag.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* SLIP: C0 delimiter, DB DC escaped C0, DB DD escaped DB.
 * Body: version:u8,type:u8,length:u16LE,sequence:u32LE,payload,crc16LE.
 * CRC-16/CCITT-FALSE over body excluding CRC. */
#define SAMPLES 384
typedef struct { uint16_t count; int64_t time; int16_t pcm[SAMPLES]; } block_t;
static QueueHandle_t queue;
static atomic_uint last_heartbeat_ms;
static uint32_t sequence;
static atomic_uint enqueued, dropped, stale, tx_failed, crc_failed, high_water, max_age;
static atomic_uint producer_starved;
static StaticQueue_t queue_control;
static uint8_t queue_storage[3 * sizeof(block_t)];
static TaskHandle_t tx_task;
static uint16_t crc16(const uint8_t *p, size_t n)
{
    uint16_t crc = 0xffff;
    while (n--) {
        crc ^= (uint16_t)*p++ << 8;
        for (int i=0;i<8;i++) crc = (crc & 0x8000) ? (crc<<1)^0x1021 : crc<<1;
    }
    return crc;
}
static bool active(void)
{
    uint32_t last = atomic_load(&last_heartbeat_ms);
    return last && (uint32_t)((uint32_t)(esp_timer_get_time()/1000)-last) < 1000;
}
bool retro_audio_connected(void) { return active(); }
void retro_audio_discard_pending(void) { if (queue) xQueueReset(queue); }
static void send_packet(uint8_t type, const void *payload, uint16_t len)
{
    uint8_t raw[10+SAMPLES*2], wire[2*(10+SAMPLES*2)+2];
    raw[0]=1; raw[1]=type; raw[2]=len; raw[3]=len>>8;
    uint32_t seq=sequence++;
    for(int i=0;i<4;i++) raw[4+i]=seq>>(8*i);
    memcpy(raw+8,payload,len);
    uint16_t crc=crc16(raw,8+len);
    raw[8+len]=crc; raw[9+len]=crc>>8;
    size_t n=0; wire[n++]=0xc0;
    for(size_t i=0;i<len+10;i++) {
        if(raw[i]==0xc0 || raw[i]==0xdb) {
            wire[n++]=0xdb; wire[n++]=raw[i]==0xc0 ? 0xdc : 0xdd;
        } else wire[n++]=raw[i];
    }
    wire[n++]=0xc0;
    if (usb_serial_jtag_write_bytes(wire,n,pdMS_TO_TICKS(5)) != n)
        atomic_fetch_add(&tx_failed, 1);
}
static void send_stats(void)
{
    uint32_t values[] = {
#if CONFIG_RETRO_USB_AUDIO_TEST_TONE
        1,
#else
        0,
#endif
        atomic_load(&enqueued), atomic_load(&dropped), atomic_load(&stale),
        atomic_load(&tx_failed), atomic_load(&crc_failed), atomic_load(&high_water),
        atomic_load(&max_age), heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        uxTaskGetStackHighWaterMark(NULL), atomic_load(&producer_starved)
    };
    send_packet(6, values, sizeof(values));
}
static void transmit(void *arg)
{
    block_t block;
    int64_t status_time=0;
    for(;;) {
        bool received = xQueueReceive(queue,&block,pdMS_TO_TICKS(20))==pdTRUE;
        if (active()) {
            if(esp_timer_get_time()-status_time > 1000000) {
                /* rate:u32LE, channels:u8, bits:u8 */
                const uint8_t format[]={0x22,0x56,0,0,1,16};
                send_packet(2,format,sizeof(format));
                send_stats();
                status_time=esp_timer_get_time();
            }
            if (received) {
                uint32_t age = esp_timer_get_time()-block.time;
                if (age > atomic_load(&max_age)) atomic_store(&max_age, age);
                if (age < 60000) send_packet(1,block.pcm,block.count*2);
                else atomic_fetch_add(&stale, 1);
            } else atomic_fetch_add(&producer_starved, 1);
        }
    }
}
static void enqueue(const int16_t *samples, size_t count);
#if CONFIG_RETRO_USB_AUDIO_TEST_TONE
static void tone(void *arg)
{
    /* Allocated once on the task stack, never per block. ~12.5% full scale. */
    int16_t wave[256], pcm[221];
    for (unsigned i=0; i<256; i++) wave[i] = (int16_t)(4096*sinf(i*6.28318530718f/256));
    uint32_t phase=0, remainder=0;
    const uint32_t step = ((uint64_t)440 << 32) / RETRO_AUDIO_RATE;
    TickType_t wake=xTaskGetTickCount();
    for (;;) {
        if (active()) {
            remainder += RETRO_AUDIO_RATE;
            unsigned count = remainder / 100;
            remainder %= 100;
            for (unsigned i=0; i<count; i++) { pcm[i]=wave[phase>>24]; phase+=step; }
            enqueue(pcm, count);
        }
        /* 10 ms at the project's 100 Hz FreeRTOS tick rate. No USB writes here. */
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(10));
    }
}
#endif
bool retro_audio_init(void)
{
    if(queue) return true;
    queue=xQueueCreateStatic(3,sizeof(block_t),queue_storage,&queue_control);
    if(!queue) return false;
    if(xTaskCreate(transmit,"usb_audio",6144,NULL,3,&tx_task)!=pdPASS) {
        vQueueDelete(queue); queue=NULL; return false;
    }
#if CONFIG_RETRO_USB_AUDIO_TEST_TONE
    if(xTaskCreate(tone,"usb_tone",4096,NULL,2,NULL)!=pdPASS) {
        vTaskDelete(tx_task); vQueueDelete(queue); queue=NULL; return false;
    }
#endif
    return true;
}
void retro_audio_submit(const int16_t *samples,size_t count)
{
#if !CONFIG_RETRO_USB_AUDIO_TEST_TONE
    enqueue(samples, count);
#endif
}
static void enqueue(const int16_t *samples,size_t count)
{
    if(!queue || !active()) return;
    block_t block, discarded;
    while(count) {
        size_t n=count>SAMPLES ? SAMPLES : count;
        block.count=n; block.time=esp_timer_get_time();
        memcpy(block.pcm,samples,n*2);
        if(xQueueSend(queue,&block,0)!=pdTRUE) {
            if (xQueueReceive(queue,&discarded,0)==pdTRUE) atomic_fetch_add(&dropped, 1);
            if (xQueueSend(queue,&block,0)!=pdTRUE) atomic_fetch_add(&dropped, 1);
        }
        atomic_fetch_add(&enqueued, 1);
        unsigned depth=uxQueueMessagesWaiting(queue);
        if(depth > atomic_load(&high_water)) atomic_store(&high_water, depth);
        samples+=n; count-=n;
    }
}
bool retro_audio_receive(uint8_t byte, uint8_t *type, uint8_t *value)
{
    static uint8_t raw[16];
    static size_t n;
    static bool escaped, invalid;
    if(byte==0xc0) {
        bool valid=!invalid && !escaped && n==11 && raw[0]==1 && raw[2]==1 && raw[3]==0 &&
           crc16(raw,9)==(uint16_t)(raw[9]|raw[10]<<8);
        if ((n || invalid || escaped) && !valid) atomic_fetch_add(&crc_failed, 1);
        if(valid) {
            *type=raw[1]; *value=raw[8];
            if(raw[1]==3)
                atomic_store(&last_heartbeat_ms,raw[8] ? (uint32_t)(esp_timer_get_time()/1000) : 0);
        }
        n=0; escaped=false; invalid=false; return valid;
    }
    if(byte==0xdb) { if(escaped) invalid=true; escaped=true; return false; }
    if(escaped) {
        if(byte!=0xdc && byte!=0xdd) invalid=true;
        byte=byte==0xdc ? 0xc0 : 0xdb; escaped=false;
    }
    if(n<sizeof(raw)) raw[n++]=byte; else invalid=true;
    return false;
}
