#include "stm32f4xx.h"
#include <string.h>
#include "lfs.h"
#include "W25Q16JVSNIQ_driver.h"

static lfs_t lfs;
static lfs_file_t file;

static uint8_t lfs_read_buf[64];
static uint8_t lfs_prog_buf[64];
static uint8_t lfs_lookahead_buf[16];

static int lfs_w25q16_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
    uint32_t addr = (block * c->block_size) + off;
    W25Q16_Read_Data(addr, (uint8_t*)buffer, size);
    return LFS_ERR_OK;
}

static int lfs_w25q16_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
    uint32_t addr = (block * c->block_size) + off;
    uint32_t bytes_written = 0;
    uint8_t *pData = (uint8_t*)buffer;

    while (bytes_written < size)
    {
        uint32_t chunk_size = size - bytes_written;
        if (chunk_size > 256)
        {
            chunk_size = 256;
        }
        W25Q16_Write_Page(addr + bytes_written, &pData[bytes_written], chunk_size);
        bytes_written += chunk_size;
    }
    return LFS_ERR_OK;
}

static int lfs_w25q16_erase(const struct lfs_config *c, lfs_block_t block)
{
    uint32_t addr = block * c->block_size;
    W25Q16_Erase_Sector(addr);
    return LFS_ERR_OK;
}

static int lfs_w25q16_sync(const struct lfs_config *c)
{
    (void)c;
    return LFS_ERR_OK;
}

static const struct lfs_config cfg =
{
    .read  = lfs_w25q16_read,
    .prog  = lfs_w25q16_prog,
    .erase = lfs_w25q16_erase,
    .sync  = lfs_w25q16_sync,
    
    .read_size = 16,
    .prog_size = 16,
    .block_size = 4096,
    .block_count = 512,
    
    .cache_size = 64,
    .lookahead_size = 16,
    .read_buffer = lfs_read_buf,
    .prog_buffer = lfs_prog_buf,
    .lookahead_buffer = lfs_lookahead_buf,
    
    .block_cycles = 500,
};

#if 0
void check_littlefs_file(void)
{
    int err = lfs_mount(&lfs, &cfg);
    if (err == LFS_ERR_OK)
    {
        err = lfs_file_opencfg(&lfs, &file, "received.bin", LFS_O_RDONLY, NULL);
        if (err == LFS_ERR_OK)
        {
            GPIOC->ODR &= ~(1UL << 13);
            for(volatile uint32_t i = 0; i < 15000000; i++);
            GPIOC->ODR |= (1UL << 13);
            lfs_file_close(&lfs, &file);
        }
        lfs_unmount(&lfs);
    }
}
#endif

void save_to_littlefs(const uint8_t *data, uint32_t length)
{
    int err = lfs_mount(&lfs, &cfg);
    if (err)
    {
        lfs_format(&lfs, &cfg);
        err = lfs_mount(&lfs, &cfg);
    }
    if (err == LFS_ERR_OK)
    {
        err = lfs_file_opencfg(&lfs, &file, "received.bin", LFS_O_CREAT | LFS_O_WRONLY | LFS_O_TRUNC, NULL);

        if (err == LFS_ERR_OK)
        {
            lfs_file_write(&lfs, &file, data, length);
            lfs_file_close(&lfs, &file);
        }
        lfs_unmount(&lfs);
    }
}
