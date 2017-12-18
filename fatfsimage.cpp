// Copyright 2017-2018 Leland Lucius
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "sdkconfig.h"

#include "argtable3.h"
#include "diskio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "ff.h"
#include "WL_Flash.h"

// Copied from "esp-idf/components/wear_leveling/wear_leveling.cpp"
#ifndef MAX_WL_HANDLES
#define MAX_WL_HANDLES 8
#endif // MAX_WL_HANDLES

#ifndef WL_DEFAULT_UPDATERATE
#define WL_DEFAULT_UPDATERATE   16
#endif //WL_DEFAULT_UPDATERATE

#ifndef WL_DEFAULT_TEMP_BUFF_SIZE
#define WL_DEFAULT_TEMP_BUFF_SIZE   32
#endif //WL_DEFAULT_TEMP_BUFF_SIZE

#ifndef WL_DEFAULT_WRITE_SIZE
#define WL_DEFAULT_WRITE_SIZE   16
#endif //WL_DEFAULT_WRITE_SIZE

#ifndef WL_DEFAULT_START_ADDR
#define WL_DEFAULT_START_ADDR   0
#endif //WL_DEFAULT_START_ADDR

#ifndef WL_CURRENT_VERSION
#define WL_CURRENT_VERSION  1
#endif //WL_CURRENT_VERSION

static const char TAG[] = "FatFSImage";
static const char drv[] = "FatFSImage";
static WL_Flash flash;

class FatFSImage : public Flash_Access
{
private:
    typedef struct
    {
        const char *srcbase;
        const char *dstbase;
        int srclen;
        int dstlen;
        char src[PATH_MAX];
        char dst[PATH_MAX];
        char buf[SPI_FLASH_SEC_SIZE];
    } copy_state;

public:
    FatFSImage();
    virtual ~FatFSImage();

    esp_err_t main(int argc, char *argv[]);
    esp_err_t parse(int argc, char *argv[]);
    esp_err_t init_wear_levelling();
    esp_err_t create_image();
    esp_err_t create_filesystem();
    esp_err_t load_files();
    esp_err_t copy(const char *src, const char *dst);
    esp_err_t copy_sub(copy_state *cs);

    //
    // Flash_Access implementaion
    //
    virtual size_t chip_size() final;
    virtual esp_err_t erase_sector(size_t sector) final;
    virtual esp_err_t erase_range(size_t start_address, size_t size) final;
    virtual esp_err_t write(size_t dest_addr, const void *src, size_t size) final;
    virtual esp_err_t read(size_t src_addr, void *dest, size_t size) final;
    virtual size_t sector_size() final;

private:
    struct
    {
        struct arg_lit *help;
        struct arg_int *level;
        struct arg_file *image;
        struct arg_int *kb;
        struct arg_file *paths;
        struct arg_end *end;
    }
    args =
    {
        arg_litn("h", "help", 0, 1, "display this help and exit"),
        arg_intn("l", "log", "<level>", 0, 1, "log level (0-5, 3 is default)"),
        arg_filen(NULL, NULL, "<image>", 1, 1, "image file name"),
        arg_intn(NULL, NULL, "<KB>", 1, 1, "disk size in KB"),
        arg_filen(NULL, NULL, "<paths>", 1, 20, "directories/files to load"),
        arg_end(1),
    };
    void **argtable = (void **) &args; // shame on me ;-)
    static const int argcount = sizeof(args) / sizeof(void *);

    FILE *image;
    FATFS *fs;
    uint32_t image_bytes = 0;
    uint32_t sector_bytes = 0;
    uint32_t sector_count = 0;
    uint32_t numdirs = 0;
    uint32_t numfiles = 0;
};

FatFSImage::FatFSImage()
{
    sector_bytes = SPI_FLASH_SEC_SIZE;
    image = NULL;
    fs = NULL;
    numdirs = 0;
    numfiles = 0;
}

FatFSImage::~FatFSImage()
{
}

int FatFSImage::main(int argc, char *argv[])
{
    esp_err_t err = ESP_FAIL;

    if (parse(argc, argv) == ESP_OK)
    {
        if (create_image() == ESP_OK)
        {
            if (init_wear_levelling() == ESP_OK)
            {
                if (create_filesystem() == ESP_OK)
                {
                    if (load_files() == ESP_OK)
                    {
                        FATFS *fs;
                        DWORD nfree = 0;
                        FRESULT res = f_getfree(drv, &nfree, &fs);
                        printf("Filesystem created\n\n");
                        printf("  directories created: %d\n", numdirs);
                        printf("  files copied: %d\n", numfiles);
                        printf("\n");

                        printf("  flash sector size: %d\n", SPI_FLASH_SEC_SIZE);
                        printf("  flash sectors: %d\n", image_bytes / SPI_FLASH_SEC_SIZE);
                        printf("\n");
                        printf("  filesystem sector size: %d\n", fs->ssize);
                        printf("  filesystem sectors: %d\n", image_bytes / fs->ssize);
                        printf("  filesystem cluster size: %d\n", fs->csize * fs->ssize);
                        printf("  filesystem total clusters: %d\n", fs->n_fatent - 2);
                        printf("  filesystem free clusters: %d\n", nfree);
                        
                        err = ESP_OK;
                    }
                    
                    f_unmount(drv);
                    delete fs;
                }
            }

            fclose(image);
        }

        arg_freetable(argtable, argcount);
    }

    return err;
}

esp_err_t FatFSImage::parse(int argc, char *argv[])
{
    esp_err_t err;

    int err_cnt = arg_parse(argc, argv, argtable);

    if (args.help->count > 0)
    {
        printf("Usage: %s", argv[0]);
        arg_print_syntax(stdout, argtable, "\n");

        printf("Create and load a FATFS disk image.\n\n");
        arg_print_glossary(stdout, argtable, "  %-25s %s\n");

        err = ESP_FAIL;
    }
    else if (err_cnt > 0)
    {
        /* Display the error details contained in the arg_end struct.*/
        arg_print_errors(stdout, args.end, argv[0]);

        printf("\nUsage: %s", argv[0]);
        arg_print_syntax(stdout, argtable, "\n");

        err = ESP_FAIL;
    }
    else
    {
        image_bytes = args.kb->ival[0] * 1024;
        sector_count = image_bytes / sector_bytes;

        if (args.level->count > 0)
        {
            int level = args.level->ival[0];
            if (level < ESP_LOG_NONE)
            {
                level = ESP_LOG_NONE;
            }
            else if (level > ESP_LOG_VERBOSE)
            {
                level = ESP_LOG_VERBOSE;
            }

            esp_log_level_set(TAG, (esp_log_level_t) level);
        }

        err = ESP_OK;
    }

    return err;
}

esp_err_t FatFSImage::create_image()
{
    ESP_LOGD(TAG, "Creating '%s' with %d bytes", args.image->filename[0], image_bytes);

    image = fopen(args.image->filename[0], "w+");
    if (image == NULL)
    {
        ESP_LOGE(TAG, "Open failed with %d for '%s'", errno, args.image->filename[0]);
        return ESP_FAIL;
    }

    char buf[SPI_FLASH_SEC_SIZE];
    int bytes = image_bytes;

    memset(buf, 0xff, sizeof(buf));

    for (int i = 0, len = 0; i < bytes; i += len)
    {
        len = bytes > sizeof(buf) ? sizeof(buf) : bytes;

        fwrite(buf, 1, len, image);
        if (ferror(image))
        {
            ESP_LOGE(TAG, "Write failed with %d for '%s'", errno, args.image->filename);
            return ESP_FAIL;
        }
    }
    
    return ESP_OK;
}

esp_err_t FatFSImage::init_wear_levelling()
{
    ESP_LOGD(TAG, "Initalizing wear levelling");

    esp_err_t err = ESP_OK;

    wl_config_t cfg =
    {
        .start_addr = WL_DEFAULT_START_ADDR,
        .full_mem_size = image_bytes,
        .page_size = SPI_FLASH_SEC_SIZE,
        .sector_size = SPI_FLASH_SEC_SIZE,
        .updaterate = WL_DEFAULT_UPDATERATE,
        .wr_size = WL_DEFAULT_WRITE_SIZE,
        .version = WL_CURRENT_VERSION,
        .temp_buff_size = WL_DEFAULT_TEMP_BUFF_SIZE,
        .crc = 0
    };

    err = flash.config(&cfg, this);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Wear levelling configuration failed with %d", err);
        return err;
    }

    err = flash.init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Wear levelling initialization failed with %d", err);
        return err;
    }

    return ESP_OK;
}

esp_err_t FatFSImage::create_filesystem()
{
    ESP_LOGD(TAG, "Creating filesystem within image");

    esp_err_t err;
    FRESULT res;

    res = f_mkfs(drv, FM_ANY | FM_SFD, 0, NULL, 0);
    if (res != FR_OK)
    {
        ESP_LOGE(TAG, "Filesystem creation failed with %d", res);
        return ESP_FAIL;
    }

    FATFS *f = new FATFS;
    if (f == nullptr)
    {
        ESP_LOGE(TAG, "Insufficent memory for FATFS");
        return ESP_FAIL;
    }

    res = f_mount(f, drv, 0);
    if (res != FR_OK)
    {
        delete f;

        ESP_LOGE(TAG, "Mounting filesystem failed with %d", res);
        return ESP_FAIL;
    }

    fs = f;

    return ESP_OK;
}

esp_err_t FatFSImage::load_files()
{
    ESP_LOGD(TAG, "Loading files");

    for (int i = 0; i < args.paths->count; ++i)
    {
        copy(args.paths->filename[i], "");
    }

    return ESP_OK;
}

int FatFSImage::copy(const char *src, const char *dst)
{
    ESP_LOGD(TAG, "Processing '%s'", src);

    int err = 0;

    copy_state *cs = (copy_state *) malloc(sizeof(copy_state));
    if (cs == NULL)
    {
        ESP_LOGE(TAG, "Unable to allocate memory");
        return -1;
    }

    int len;
    for (len = 0; src[len] && len < sizeof(cs->src); len++)
    {
        cs->src[len] = src[len];
    }

    if (len == sizeof(cs->src))
    {
        ESP_LOGE(TAG, "Source name '%s' is too long", src);
        free(cs);
        return -1;
    }
    cs->src[len] = '\0';
    cs->srclen = len;

    for (len = 0; dst[len] && len < sizeof(cs->dst); len++)
    {
        cs->dst[len] = dst[len];
    }

    if (len == sizeof(cs->dst))
    {
        ESP_LOGE(TAG, "Target name '%s' is too long", dst);
        free(cs);
        return -1;
    }
    cs->dst[len] = '\0';
    cs->dstlen = len;

    cs->srcbase = src;
    cs->dstbase = dst;

    err = copy_sub(cs);

    free(cs);

    return err;
}

int FatFSImage::copy_sub(copy_state *cs)
{
    FRESULT res = FR_OK;
    int err = 0;

    struct stat s;
    if (stat(cs->src, &s) == -1)
    {
        ESP_LOGE(TAG, "Unable to get file info for '%s'", cs->src);
        return -1;
    }

    if (S_ISDIR(s.st_mode))
    {
        FILINFO fno;
        res = f_stat(cs->dst, &fno);
        if (res == FR_OK && !(fno.fattrib & AM_DIR))
        {
            ESP_LOGE(TAG, "Attempt to copy directory '%s' to non-directorys '%s'", cs->src, cs->dst);
            return -1;
        }

        if (res == FR_NO_FILE)
        {
            ESP_LOGD(TAG, "Creating directory '%s'", cs->dst);
            err = f_mkdir(cs->dst);
            if (err == FR_OK)
            { 
                numdirs++;
            }
        }

        if (err != FR_OK)
        {
            ESP_LOGE(TAG, "Unable to create directory '%s'", cs->dst);
            return -1;
        }

        DIR *dirp = opendir(cs->src);
        if (dirp != NULL)
        {
            while (1)
            {
                struct dirent *dp = readdir(dirp);
                if (dp == NULL)
                {
                    break;
                }

                if (strcmp(dp->d_name, ".") == 0 ||
                    strcmp(dp->d_name, "..") == 0)
                {
                    continue;
                }

                int srcorig = cs->srclen;
                int dstorig = cs->dstlen;

                cs->srclen += 1 + strlen(dp->d_name);
                cs->dstlen += 1 + strlen(dp->d_name);

                if (cs->srclen >= PATH_MAX)
                {
                    ESP_LOGE(TAG, "Source name '%s/%s' is too long", cs->src, dp->d_name);
                    err = -1;
                }
                else if (cs->dstlen >= PATH_MAX)
                {
                    ESP_LOGE(TAG, "Target name '%s/%s' is too long", cs->dst, dp->d_name);
                    err = -1;
                }
                else
                {
                    cs->src[srcorig] = '/';
                    strcpy(&cs->src[srcorig + 1], dp->d_name);

                    cs->dst[dstorig] = '/';
                    strcpy(&cs->dst[dstorig + 1], dp->d_name);

                    err = copy_sub(cs);

                    cs->src[srcorig] = '\0';
                    cs->dst[dstorig] = '\0';
                }

                cs->srclen = srcorig;
                cs->dstlen = dstorig;
            }

            closedir(dirp);
        }
    }
    else if (!S_ISREG(s.st_mode))
    {
        ESP_LOGE(TAG, "'%s' is not a normal file or directory", cs->src);
        err = -1;
    }
    else
    {
        FILINFO fno;

        res = f_stat(cs->dst, &fno);
        if (res == FR_OK && fno.fattrib & AM_DIR)
        {
            int dstorig = cs->dstlen;
            char *p = &cs->src[cs->srclen];
            do
            {
                cs->dstlen++;
            } while (*--p != '/');

            if (cs->dstlen >= PATH_MAX)
            {
                ESP_LOGE(TAG, "Target name '%s/%s' is too long", cs->dst, p + 1);
                err = -1;
            }
            else
            {
                cs->dst[dstorig] = '/';
                strcpy(&cs->dst[dstorig + 1], p + 1);
            }
        }

        if (res != FR_NO_FILE)
        {
            ESP_LOGE(TAG, "Unable to create destination file '%s'", cs->dst);
            return -1;
        }

        ESP_LOGD(TAG, "Copying file '%s' to '%s'", cs->src, cs->dst);

        FILE *srcf = fopen(cs->src, "rb");
        if (srcf == NULL)
        {
            ESP_LOGE(TAG, "Unable to open source '%s'", cs->src);
            return -1;
        }
        else
        {
            FIL dstf;

            res = f_open(&dstf, cs->dst, FA_WRITE | FA_CREATE_ALWAYS);
            if (res != FR_OK)
            {
                ESP_LOGE(TAG, "Unable to open target '%s'", cs->dst);
                err = -1;
            }
            else
            {
                err = 0;
                while (!feof(srcf) && !ferror(srcf) && f_error(&dstf) == FR_OK)
                {
                    UINT bw;
                    size_t read = fread(cs->buf, 1, sizeof(cs->buf), srcf);
                    f_write(&dstf, cs->buf, read, &bw);
                }

                if (ferror(srcf))
                {
                    ESP_LOGE(TAG, "Read returned %d for source '%s'", errno, cs->src);
                    err = -1;
                }
                else if (f_error(&dstf) != FR_OK)
                {
                    ESP_LOGE(TAG, "Write returned %d for target '%s'", f_error(&dstf), cs->src);
                    err = -1;
                }
                else
                {
                    numfiles++;
                }

                f_close(&dstf);

                if (err == -1)
                {
                    f_unlink(cs->dst);
                    // ignore errors
                }
            }
            fclose(srcf);
        }
    }

    return err;
}


// ============================================================================
// Flash_Access implementation
// ============================================================================

size_t FatFSImage::chip_size()
{
    ESP_LOGV(TAG, "%s - %d", __func__, image_bytes);

    return image_bytes;
}

esp_err_t FatFSImage::erase_sector(size_t sector)
{
    ESP_LOGV(TAG, "%s - sector=0x%08x", __func__, (uint32_t) sector);

    return erase_range(sector * sector_size(), sector_size());
}

esp_err_t FatFSImage::erase_range(size_t start_address, size_t size)
{
    ESP_LOGV(TAG, "%s - add=0x%08x size=%d", __func__, (uint32_t) start_address, size);

    if (fseek(image, start_address, SEEK_SET) == -1)
    {
        return RES_ERROR;
    }

    char buf[SPI_FLASH_SEC_SIZE];
    int bytes = size;

    memset(buf, 0xff, sizeof(buf));

    for (int i = 0, len = 0; i < bytes; i += len)
    {
        len = bytes > sizeof(buf) ? sizeof(buf) : bytes;

        fwrite(buf, 1, len, image);
        if (ferror(image))
        {
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

esp_err_t FatFSImage::write(size_t addr, const void *src, size_t size)
{
    ESP_LOGV(TAG, "%s - addr=0x%08x size=%d", __func__, (uint32_t) addr, size);

    if (fseek(image, addr, SEEK_SET) == -1)
    {
        return RES_ERROR;
    }

    size_t written = fwrite(src, 1, size, image);
    if (written == size)
    {
        return RES_OK;
    }

    return ESP_OK;
}

esp_err_t FatFSImage::read(size_t addr, void *dest, size_t size)
{
    ESP_LOGV(TAG, "%s - addr=0x%08x size=%d", __func__, (uint32_t) addr, size);

    if (fseek(image, addr, SEEK_SET) == -1)
    {
        return RES_ERROR;
    }

    size_t read = fread(dest, 1, size, image);
    if (read == size)
    {
        return RES_OK;
    }

    return ESP_OK;
}

size_t FatFSImage::sector_size()
{
    ESP_LOGV(TAG, "%s - %d", __func__, sector_bytes);

    return sector_bytes;
}

// ============================================================================
// The rest is plain old "C" code
// ============================================================================
extern "C"
{

// ============================================================================
// FATFS diskio implementation
// ============================================================================

#if FF_MULTI_PARTITION      /* Multiple partition configuration */
PARTITION VolToPart[] = {
    {0, 0},    /* Logical drive 0 ==> Physical drive 0, auto detection */
    {1, 0}     /* Logical drive 1 ==> Physical drive 1, auto detection */
};
#endif

DSTATUS disk_initialize(BYTE pdrv)
{
    ESP_LOGV(TAG, "%s - pdrv=%d", __func__, pdrv);

    return 0;
}

DSTATUS disk_status(BYTE pdrv)
{
    ESP_LOGV(TAG, "%s - pdrv=%d", __func__, pdrv);

    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    ESP_LOGV(TAG, "%s - pdrv=%d, sector=%ld, count=%d", __func__, pdrv, sector, count);

    size_t ss = flash.sector_size();
    size_t addr = sector * ss;
    size_t len = count * ss;
    esp_err_t err;

    err = flash.read(addr, buff, len);
    if (err != ESP_OK)
    {
        return RES_ERROR;
    }

    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    ESP_LOGV(TAG, "%s - pdrv=%d, sector=%ld, count=%d", __func__, pdrv, sector, count);

    size_t ss = flash.sector_size();
    size_t addr = sector * ss;
    size_t len = count * ss;
    esp_err_t err;

    err = flash.erase_range(addr, len);
    if (err != ESP_OK)
    {
        return RES_ERROR;
    }

    err = flash.write(addr, buff, len);
    if (err != ESP_OK)
    {
        return RES_ERROR;
    }

    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    ESP_LOGV(TAG, "%s: cmd=%d", __func__, cmd);

    switch (cmd)
    {
        case CTRL_SYNC:
            return RES_OK;

        case GET_SECTOR_COUNT:
            *((DWORD *) buff) = flash.chip_size() / flash.sector_size();
            return RES_OK;

        case GET_SECTOR_SIZE:
            *((WORD *) buff) = flash.sector_size();
            return RES_OK;

        case GET_BLOCK_SIZE:
            return RES_ERROR;
    }

    return RES_ERROR;
}

DWORD get_fattime(void)
{
    time_t t = time(NULL);
    struct tm tmr;
    localtime_r(&t, &tmr);
    int year = tmr.tm_year < 80 ? 0 : tmr.tm_year - 80;
    return    ((DWORD)(year) << 25)
            | ((DWORD)(tmr.tm_mon + 1) << 21)
            | ((DWORD)tmr.tm_mday << 16)
            | (WORD)(tmr.tm_hour << 11)
            | (WORD)(tmr.tm_min << 5)
            | (WORD)(tmr.tm_sec >> 1);
}

// ============================================================================
// ESP logging replacements
// ============================================================================

static esp_log_level_t esp_log_level = ESP_LOG_INFO;

void esp_log_level_set(const char* tag, esp_log_level_t level)
{
    esp_log_level = level;
}

uint32_t esp_log_timestamp()
{
    return 0;
}

void esp_log_write(esp_log_level_t level, const char* tag, const char* format, ...)
{
    if (esp_log_level >= level)
    {
        if (tag == TAG || esp_log_level == ESP_LOG_VERBOSE)
        {
            va_list list;
            va_start(list, format);
            vprintf(format, list);
            va_end(list);
        }
    }
}

// ============================================================================
// Ye' old main
// ============================================================================

int main(int argc, char *argv[])
{
    FatFSImage ffsi;

    return ffsi.main(argc, argv) == ESP_OK ? 0 : -1;
}

// ============================================================================
// End if "C" code
// ============================================================================
};

