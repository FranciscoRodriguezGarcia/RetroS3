#include "rom_manager.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

#include "esp_log.h"

static const char *TAG = "ROM_MANAGER";

static emulator_system_t detectar_sistema(
    const char *filename
)
{
    const char *ext =
        strrchr(filename, '.');

    if (ext == NULL)
    {
        return SYSTEM_UNKNOWN;
    }


    if (
        strcasecmp(ext, ".gb") == 0
    )
    {
        return SYSTEM_GAMEBOY;
    }

    if (
        strcasecmp(ext, ".gbc") == 0
    )
    {
        return SYSTEM_GAMEBOY_COLOR;
    }

    if (
        strcasecmp(ext, ".nes") == 0
    )
    {
        return SYSTEM_NES;
    }

    if (strcasecmp(ext, ".md") == 0 ||
        strcasecmp(ext, ".gen") == 0)
    {
        return SYSTEM_MEGADRIVE;
    }

    return SYSTEM_UNKNOWN;
}

static bool agregar_rom(
    rom_list_t *list,
    const char *folder,
    const char *filename
)
{
    if (list->count >= ROM_MAX_COUNT)
    {
        ESP_LOGW(
            TAG,
            "Limite de ROMs alcanzado"
        );
        return false;
    }

    emulator_system_t system =
        detectar_sistema(filename);

    if (system == SYSTEM_UNKNOWN)
    {
        return false;
    }

    rom_entry_t *entry =
        &list->entries[list->count];

    snprintf(
        entry->name,
        sizeof(entry->name),
        "%s",
        filename
    );

    snprintf(
        entry->path,
        sizeof(entry->path),
        "%s/%s",
        folder,
        filename
    );

    entry->system = system;

    struct stat st;

    if (stat(entry->path, &st) == 0)
    {
        entry->size =
            (size_t)st.st_size;
    }
    else
    {
        entry->size = 0;
    }

    list->count++;

    return true;
}

static void scan_tree(rom_list_t *list, const char *folder, unsigned depth)
{
    DIR *dir=opendir(folder);
    if (!dir) return;
    struct dirent *entry;
    while (list->count<ROM_MAX_COUNT && (entry=readdir(dir))!=NULL) {
        if (entry->d_name[0]=='.') continue;
        char path[ROM_PATH_MAX];
        int n=snprintf(path,sizeof(path),"%s/%s",folder,entry->d_name);
        if (n<0 || n>=sizeof(path)) { ESP_LOGW(TAG,"Ruta demasiado larga: %s",entry->d_name); continue; }
        struct stat st;
        if (stat(path,&st)!=0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth) scan_tree(list,path,depth-1);
        } else if (S_ISREG(st.st_mode)) agregar_rom(list,folder,entry->d_name);
    }
    closedir(dir);
}
static void escanear_directorio(rom_list_t *list, const char *folder)
{
    scan_tree(list,folder,2);
}

bool rom_manager_scan(
    rom_list_t *list
)
{
    if (list == NULL)
    {
        return false;
    }

    memset(
        list,
        0,
        sizeof(*list)
    );



    escanear_directorio(
        list,
        "/sdcard/GameBoy"
    );

    escanear_directorio(
        list,
        "/sdcard/GameBoyColor"
    );

    escanear_directorio(
        list,
        "/sdcard/NES"
    );

    escanear_directorio(list, "/sdcard/MegaDrive");

    escanear_directorio(list,"/sdcard/Retro53/consolas/gb/juegos");
    escanear_directorio(list,"/sdcard/Retro53/consolas/gbc/juegos");
    escanear_directorio(list,"/sdcard/Retro53/consolas/nes/juegos");
    escanear_directorio(list,"/sdcard/Retro53/consolas/md/juegos");

    ESP_LOGI(
        TAG,
        "ROMs encontradas: %u",
        (unsigned)list->count
    );

    return true;
}

const char *rom_manager_system_name(
    emulator_system_t system
)
{
    switch (system)
    {
       

        case SYSTEM_GAMEBOY:
            return "Game Boy";

        case SYSTEM_GAMEBOY_COLOR:
            return "Game Boy Color";

        case SYSTEM_NES:
            return "NES";

        case SYSTEM_MEGADRIVE:
            return "Mega Drive";

        default:
            return "Unknown";
    }
}

bool rom_manager_get_systems(const rom_list_t *roms, system_list_t *systems)
{
    if (!roms || !systems) return false;
    memset(systems,0,sizeof(*systems));
    /* The console directory is the catalogue entry, not the presence of a ROM.
     * Preserve systems found via the older nested ROM layout as a fallback. */
    static const struct { emulator_system_t system; const char *path; } folders[] = {
        {SYSTEM_NES,"/sdcard/NES"},
        {SYSTEM_GAMEBOY,"/sdcard/GameBoy"},
        {SYSTEM_GAMEBOY_COLOR,"/sdcard/GameBoyColor"},
        {SYSTEM_MEGADRIVE,"/sdcard/MegaDrive"}
    };
    for (size_t i=0;i<sizeof(folders)/sizeof(folders[0]);i++) {
        struct stat st;
        bool present=stat(folders[i].path,&st)==0 && S_ISDIR(st.st_mode);
        size_t count=0;
        for (size_t j=0;j<roms->count;j++)
            if (roms->entries[j].system==folders[i].system) count++;
        if (!present && !count) continue;
        system_entry_t *entry=&systems->entries[systems->count++];
        entry->system=folders[i].system;
        entry->rom_count=count;
        snprintf(entry->display_name,sizeof(entry->display_name),"%s",rom_manager_system_name(entry->system));
    }
    return true;
}
