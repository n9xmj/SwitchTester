/******************************************************************************
 * Flash_MX25R80.h
 *
 * Basic API for interacting with a Macronix MX25R80xx-style SPI FLASH device
 ******************************************************************************/

#ifndef FLASH_MX25R80_H
#define FLASH_MX25R80_H

//------------------------------------------------------------------------------

#define SPIFLASH_TIMEOUT                100
#define SPIFLASH_PAGE_SIZE              0x100
#define SPIFLASH_SECTOR_SIZE            0x1000
#define SPIFLASH_32K_BLOCK_SIZE         0x8000
#define SPIFLASH_FULL_BLOCK_SIZE        0x10000

/* --- SwitchTester temporary adaptation (slated for removal) -----------------
 * Wires this driver to SwitchTester's SPI3 + PA15 chip select. The directory /
 * pseudo-filesystem support from the mirror's copy has been stripped; only the
 * core SPI-NOR read / write / erase / control API remains. hspi3, SPIFLASH_NCS_*
 * come from main.h, which the .c pulls in (via device_config.h) before this.
 * -------------------------------------------------------------------------- */

#ifndef FLASH_SPI_HANDLE
#define FLASH_SPI_HANDLE                hspi3
#endif

#ifndef SPIFLASH_DEFAULT_SPI_HANDLE
#define SPIFLASH_DEFAULT_SPI_HANDLE     FLASH_SPI_HANDLE
#endif

/* Chip select on SPIFLASH_NCS (PA15), active low, plain GPIO output. */
#define SPI_FLASH_SELECT()      HAL_GPIO_WritePin(SPIFLASH_NCS_GPIO_Port, SPIFLASH_NCS_Pin, GPIO_PIN_RESET)
#define SPI_FLASH_DESELECT()    HAL_GPIO_WritePin(SPIFLASH_NCS_GPIO_Port, SPIFLASH_NCS_Pin, GPIO_PIN_SET)

//------------------------------------------------------------------------------

typedef struct PACKED
{
    uint8_t u8_manufacturer_id;
    uint8_t u8_memory_type;
    uint8_t u8_memory_density;
    uint8_t u8_reserved;
}
spiflash_id_t;

// FLASH status register

typedef union PACKED
{
    uint8_t all;
    struct
    {
        uint8_t _fill1  : 2;
        uint8_t bp      : 4;    // Block Protect 0-3 (NV)
        uint8_t _fill2  : 2;
    };
    struct
    {
        uint8_t wip     : 1;    // Write In Progress
        uint8_t wel     : 1;    // Write Latch
        uint8_t bp0     : 1;    // Block Protect 0 (NV)
        uint8_t bp1     : 1;    // Block Protect 1 (NV)
        uint8_t bp2     : 1;    // Block Protect 2 (NV)
        uint8_t bp3     : 1;    // Block Protect 3 (NV)
        uint8_t qe      : 1;    // Quad Enable (NV)
        uint8_t srwd    : 1;    // Status Register Write Disable (NV)
    };
}
spiflash_status_reg_t;

// FLASH configuration register

typedef union PACKED
{
    uint16_t all;
    struct
    {
        uint8_t cr1;
        uint8_t cr2;
    };
    struct
    {
        uint8_t _fill1  : 3;
        uint8_t tb      : 1;    // Top/Bottom protect (OTP)
        uint8_t _fill2  : 2;
        uint8_t dc      : 1;    // Dummy Cycle select
        uint8_t _fill3  : 1;

        uint8_t _fill4  : 1;
        uint8_t lh      : 1;    // Low power / High performance mode select
        uint8_t _fill5  : 6;
    };
}
spiflash_config_reg_t;

// FLASH security status register

typedef union PACKED
{
    uint8_t all;

    struct
    {
        uint8_t fldso   : 1;    // Factory OTP lockdown (OTP)
        uint8_t ldso    : 1;    // Customer OTP lockdown (OTP)
        uint8_t psb     : 1;    // Program suspend
        uint8_t esb     : 1;    // Erase suspend
        uint8_t _res4   : 1;    // (reserved)
        uint8_t p_fail  : 1;    // Program fail
        uint8_t e_fail  : 1;    // Erase fail
        uint8_t _res7   : 1;    // (reserved)
    };
}
spiflash_security_reg_t;

//------------------------------------------------------------------------------

extern void v_spiflash_set_bus_handle(SPI_HandleTypeDef *p_x_spi_handle);

extern void v_spiflash_select(void);
extern void v_spiflash_deselect(void);

extern uint8_t u8_spiflash_write_enable(void);
extern uint8_t u8_spiflash_write_disable(void);
extern uint8_t u8_spiflash_powerdown(void);
extern uint8_t u8_spiflash_powerup(void);
extern uint8_t u8_spiflash_reset(void);
extern uint8_t u8_spiflash_read_id(spiflash_id_t *p_x_id_data);
extern uint8_t u8_spiflash_read_status(spiflash_status_reg_t *p_x_status);
extern uint8_t u8_spiflash_write_wait(uint16_t u16_timeout);
extern uint8_t u8_spiflash_read_config(spiflash_config_reg_t *p_x_config);
extern uint8_t u8_spiflash_write_config(const spiflash_status_reg_t *p_x_status, const spiflash_config_reg_t *p_x_config);
extern uint8_t u8_spiflash_read_security_reg(spiflash_security_reg_t *p_x_secreg);

extern uint8_t u8_spiflash_sector_erase(uint32_t u32_address);
extern uint8_t u8_spiflash_32k_block_erase(uint32_t u32_address);
extern uint8_t u8_spiflash_block_erase(uint32_t u32_address);
extern uint8_t u8_spiflash_chip_erase(void);

extern uint8_t u8_spiflash_read(void *v_read_data, uint32_t u32_address, uint16_t u16_size);
extern uint8_t u8_spiflash_write_page(void *v_write_data, uint32_t u32_address, uint16_t u16_size);
extern uint8_t u8_spiflash_write(void *v_write_data, uint32_t u32_address, uint16_t u16_size, uint8_t u8_erase_option);

#endif
