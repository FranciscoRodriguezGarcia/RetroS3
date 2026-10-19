#include "ui_media.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "input.h"
#include "retro_audio.h"

/* R53A v1: LE header, BE RGB565 palette, delta rectangles with index RLE.
 * All file structure and packet bounds are checked before playback. */
struct ui_animation {
    uint8_t *file;
    uint16_t *pixels;
    size_t size, offset;
    uint16_t width, height, count, frame;
    uint16_t hold_after;
    bool held;
    uint32_t duration;
    int64_t next_us;
};
static unsigned u16(const uint8_t *p) { return p[0] | ((unsigned)p[1]<<8); }
static uint32_t u32(const uint8_t *p) { return u16(p) | ((uint32_t)u16(p+2)<<16); }
static void *read_asset(const char *path, size_t limit, size_t *size)
{
    FILE *f=fopen(path,"rb");
    if (!f) return NULL;
    if (fseek(f,0,SEEK_END)!=0) { fclose(f); return NULL; }
    long length=ftell(f);
    if (length<=0 || length>limit || fseek(f,0,SEEK_SET)!=0) { fclose(f); return NULL; }
    uint8_t *data=heap_caps_malloc(length,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    bool ok=data && fread(data,1,length,f)==length;
    fclose(f);
    if (!ok) { free(data); return NULL; }
    *size=length;
    return data;
}
static bool rectangle(ui_animation_t *a, size_t *offset, bool draw)
{
    size_t pos=*offset;
    if (pos>a->size || a->size-pos<14) return false;
    const uint8_t *h=a->file+pos;
    unsigned delay=u16(h), x=u16(h+2), y=u16(h+4), w=u16(h+6), height=u16(h+8);
    size_t len=u32(h+10), used=0, decoded=0;
    pos+=14;
    if (delay<20 || delay>2000 || x+w>a->width || y+height>a->height || len>a->size-pos)
        return false;
    const uint8_t *data=a->file+pos;
    size_t total=(size_t)w*height;
    while (used<len) {
        uint8_t control=data[used++];
        unsigned n=(control&127)+1;
        bool run=(control&128)!=0;
        size_t bytes=run ? 1 : n;
        if (bytes>len-used || n>total-decoded) return false;
        if (draw) for (unsigned i=0;i<n;i++) {
            uint8_t color=data[used+(run ? 0 : i)];
            /* Palette bytes already match the display's wire byte order. */
            memcpy(a->pixels+(y+(decoded+i)/w)*a->width+x+(decoded+i)%w,
                   a->file+16+color*2,2);
        }
        used+=bytes; decoded+=n;
    }
    if (decoded!=total) return false;
    *offset=pos+len;
    return true;
}
ui_animation_t *ui_animation_load(const char *path)
{
    ui_animation_t *a=calloc(1,sizeof(*a));
    if (!a) return NULL;
    a->file=read_asset(path,2*1024*1024,&a->size);
    if (!a->file || a->size<542 || memcmp(a->file,"R53A",4)!=0) goto fail;
    a->width=u16(a->file+4); a->height=u16(a->file+6);
    a->count=u16(a->file+8); a->duration=u32(a->file+12);
    if (!a->width || a->width>320 || !a->height || a->height>160 ||
        !a->count || a->count>1000 || u16(a->file+10)!=256 ||
        a->duration<20 || a->duration>2000000) goto fail;
    size_t offset=528;
    uint32_t duration=0;
    for (unsigned i=0;i<a->count;i++) {
        if (offset>a->size || a->size-offset<14) goto fail;
        const uint8_t *h=a->file+offset;
        if (!i && (u16(h+2) || u16(h+4) || u16(h+6)!=a->width || u16(h+8)!=a->height)) goto fail;
        duration+=u16(h);
        if (!rectangle(a,&offset,false)) goto fail;
    }
    if (offset!=a->size || duration!=a->duration) goto fail;
    a->pixels=heap_caps_calloc((size_t)a->width*a->height,2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!a->pixels) goto fail;
    ui_animation_restart(a);
    return a;
fail:
    ESP_LOGW("UI_MEDIA","Recurso ausente o invalido: %s",path);
    ui_animation_free(a);
    return NULL;
}
void ui_animation_free(ui_animation_t *a)
{
    if (a) { free(a->file); free(a->pixels); free(a); }
}
void ui_animation_restart(ui_animation_t *a)
{
    if (a) { a->offset=528; a->frame=0; a->next_us=0; a->held=false; }
}
bool ui_animation_tick(ui_animation_t *a,int x,int y)
{
    if (!a || a->held || x<0 || y<0 || x+a->width>320 || y+a->height>240) return false;
    int64_t now=esp_timer_get_time();
    if (a->next_us && now<a->next_us) return false;
    if (!a->next_us) a->next_us=now;
    /* Decode skipped deltas too, but submit only the current complete image. */
    unsigned steps=0;
    do {
        unsigned delay=u16(a->file+a->offset);
        if (!rectangle(a,&a->offset,true)) return false;
        a->next_us+=(int64_t)delay*1000;
        a->frame++;
        if (a->hold_after && a->frame>=a->hold_after) { a->held=true; break; }
        if (a->frame==a->count) { a->frame=0; a->offset=528; }
    } while (a->next_us<=now && ++steps<a->count);
    if (a->next_us<=now) a->next_us=now+20000;
    return display_draw_rgb565(x,y,a->width,a->height,a->pixels)==ESP_OK;
}
int ui_animation_width(const ui_animation_t *a) { return a ? a->width : 0; }
int ui_animation_height(const ui_animation_t *a) { return a ? a->height : 0; }

void ui_play_startup(void)
{
    display_clear();
    display_draw_text(136,112,"PRESENTS",0xffff,0);
    int64_t presents_start=esp_timer_get_time();
    ui_animation_t *a=ui_animation_load("/sdcard/inicio.r53a");
    if (!a) {
        vTaskDelay(pdMS_TO_TICKS(700));
        display_clear();
        display_draw_text(130,112,"FRANTASTIC",0xffff,0);
        vTaskDelay(pdMS_TO_TICKS(600));
        return;
    }
    /* Keep the readable central logo instead of animating its disappearance. */
    a->hold_after=a->count/2+1;
    size_t pcm_bytes=0;
    int16_t *pcm=read_asset("/sdcard/inicio.pcm",441000,&pcm_bytes);
    if (pcm_bytes%2) { free(pcm); pcm=NULL; pcm_bytes=0; }
    /* A bounded grace period lets the existing Android app reattach on reboot. */
    int64_t wait_end=esp_timer_get_time()+1000000;
    while (pcm && !retro_audio_connected() && esp_timer_get_time()<wait_end) {
        if (input_get_state()&(INPUT_BUTTON_A|INPUT_BUTTON_START)) break;
        vTaskDelay(1);
    }
    bool sound=pcm && retro_audio_connected();
    size_t samples=pcm_bytes/2, cursor=0;
    uint32_t duration=a->duration;
    if (sound && samples*1000/RETRO_AUDIO_RATE>duration) duration=samples*1000/RETRO_AUDIO_RATE;
    if (duration>10000) duration=10000;
    while (esp_timer_get_time()-presents_start<700000) vTaskDelay(1);
    display_clear();
    int64_t start=esp_timer_get_time();
    input_state_t previous=input_get_state();
    bool skipped=false;
    while (esp_timer_get_time()-start<(int64_t)duration*1000) {
        input_state_t current=input_get_state();
        if (((current&~previous)&(INPUT_BUTTON_A|INPUT_BUTTON_START)) || input_consume_menu_request()) {
            skipped=true;
            break;
        }
        previous=current;
        int64_t elapsed=esp_timer_get_time()-start;
        /* The intro stops on its central frame; audio keeps its own clock. */
        if (elapsed<(int64_t)a->duration*1000)
            ui_animation_tick(a,(320-a->width)/2,(240-a->height)/2);
        if (sound && retro_audio_connected()) {
            size_t target=(uint64_t)(elapsed+20000)*RETRO_AUDIO_RATE/1000000;
            if (target>samples) target=samples;
            if (target>cursor) {
                if (target-cursor>768) cursor=target-768;
                retro_audio_submit(pcm+cursor,target-cursor);
                cursor=target;
            }
        }
        vTaskDelay(1);
    }
    if (skipped) retro_audio_discard_pending();
    else vTaskDelay(pdMS_TO_TICKS(30));
    free(pcm); ui_animation_free(a);
    /* Do not turn the button used to skip the intro into a game selection. */
    int64_t release_end=esp_timer_get_time()+300000;
    while ((input_get_state()&(INPUT_BUTTON_A|INPUT_BUTTON_START)) && esp_timer_get_time()<release_end) vTaskDelay(1);
}
