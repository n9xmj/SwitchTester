/******************************************************************************
 * Flash_MX25R80.c
 *
 * Basic API for interacting with a Macronix MX25R80xx-style SPI FLASH device
 ******************************************************************************/

#include "device_config.h"              // Includes debug_config.h, main.h, macros.h
#include "MX25R80.h"

//------------------------------------------------------------------------------

typedef enum PACKED
{
    MX_CMD_READ                 = 0x03, // Read (normal)
    MX_CMD_FASTREAD             = 0x0B, // Fast Read
    MX_CMD_2READ                = 0xBB, // 2x I/O Read
    MX_CMD_DREAD                = 0x3B, // 1xI/2xO Read
    MX_CMD_4READ                = 0xEB, // 4x I/O Read
    MX_CMD_QREAD                = 0x6B, // 1xI/4xO Read
    MX_CMD_PP                   = 0x02, // Page Program
    MX_CMD_QPP                  = 0x32, // Quad input Page Program
    MX_CMD_4PP                  = 0x38, // Quad Page Program
    MX_CMD_SE                   = 0x20, // Sector Erase
    MX_CMD_BE32K                = 0x52, // Block Erase 32K
    MX_CMD_BE                   = 0xD8, // Block Erase
    MX_CMD_CE                   = 0x60, // Chip Erase
    MX_CMD_CE_ALT               = 0xC7, // Chip Erase (alternate/alias?)
    MX_CMD_RDSFDP               = 0x5A, // Read SFDP mode

    MX_CMD_WREN                 = 0x06, // Write Enable
    MX_CMD_WRDI                 = 0x04, // Write Disable
    MX_CMD_FMEN                 = 0x41, // Factory Mode Enable
    MX_CMD_RDSR                 = 0x05, // Read Status Register
    MX_CMD_RDCR                 = 0x15, // Read Configuration Register
    MX_CMD_WRSR                 = 0x01, // Write Status and configuration Registers
    MX_CMD_PGM_SUSPEND          = 0x75, // ? Program Suspend
    MX_CMD_ERS_SUSPEND          = 0xB0, // ? Erase Suspend
    MX_CMD_PGM_RESUME           = 0x7A, // ? Program Resume
    MX_CMD_ERS_RESUME           = 0x30, // ? Erase Resume
    MX_CMD_DP                   = 0xB9, // Deep Power down
    MX_CMD_SBL                  = 0xC0, // Set Burst Length
    MX_CMD_RDFSR                = 0x44, // Read Factory Status Register

    MX_CMD_RDID                 = 0x9E, // Read Identification
    MX_CMD_RDID2                = 0x9F,
    MX_CMD_RDP                  = 0xAB, // Release from Deep Power Down mode
    MX_CMD_REMS                 = 0x90, // Read Electronic Manufacturer & Device ID
    MX_CMD_ENSO                 = 0xB1, // Enter Secured OTP
    MX_CMD_EXSO                 = 0xC1, // Exit Secured OTP
    MX_CMD_RDSCUR               = 0x2B, // Read Security Register
    MX_CMD_WRSCUR               = 0x2F, // Write Security Register
    MX_CMD_NOP                  = 0x00, // No Operation
    MX_CMD_RSTEN                = 0x66, // Reset Enable
    MX_CMD_RST                  = 0x99, // Reset Memory
}
mx_cmd_t;

//------------------------------------------------------------------------------

SPI_HandleTypeDef * p_x_spiflash_hspi = &SPIFLASH_DEFAULT_SPI_HANDLE;

/******************************************************************************
 * Control SPI FLASH slave-select line
 * v_spiflash_select()      Set SPI FLASH slave select line LOW
 * v_spiflash_deselect()    Set SPI FLASH slave select line HIGH
 ******************************************************************************/

void v_spiflash_select(void)
{
    volatile uint32_t u32_dummy;

    SPI_FLASH_DESELECT();
    u32_dummy++;                        // No-op for delay
    SPI_FLASH_SELECT();
}

void v_spiflash_deselect(void)
{
    SPI_FLASH_DESELECT();
}

/******************************************************************************
 * v_spiflash_set_bus_handle(*p_x_spi_handle)
 *
 * Set STM32 HAL SPI handle (i.e. the SPI bus to use) for SPI FLASH operations
 *
 * SPI bus handle and peripheral selected here must be initialized and ready to
 * use before any spiflash_*() functions are used.
 ******************************************************************************/

void v_spiflash_set_bus_handle(SPI_HandleTypeDef *p_x_spi_handle)
{
    p_x_spiflash_hspi = p_x_spi_handle;
}

/******************************************************************************
 * u8_spi_busy_wait(*hspi, u16_timeout)
 *
 * Wait for background (DMA or IRQ driven) SPI transaction to complete
 *
 * Parameters:
 * *hspi        Pointer to STM32 HAL SPI handle/instance
 *              (i.e. what SPI bus to check)
 * u16_timeout  Max time (mS) to wait for transaction to complete
 *
 * Returns:     STM32 HAL status/error code
 *              Returns HAL_OK (0) if transaction completes within
 *              the specified timeout.
 ******************************************************************************/

uint8_t u8_spi_busy_wait(SPI_HandleTypeDef *hspi, uint16_t u16_timeout)
{
    uint32_t u32_timestamp;
    uint32_t u32_elapsed_time;
    HAL_SPI_StateTypeDef x_state;
    uint8_t u8_busy;

    u32_timestamp = HAL_GetTick();
    do
    {
        x_state = HAL_SPI_GetState(hspi);
        u8_busy = (x_state == HAL_SPI_STATE_BUSY)
               || (x_state == HAL_SPI_STATE_BUSY_TX)
               || (x_state == HAL_SPI_STATE_BUSY_RX)
               || (x_state == HAL_SPI_STATE_BUSY_TX_RX);
        u32_elapsed_time = ELAPSED_TIME(u32_timestamp);
    }
    while ((u32_elapsed_time <= u16_timeout) && u8_busy);

    if (u8_busy)
    {
        return HAL_TIMEOUT;
    }
    return (x_state == HAL_SPI_STATE_READY) ? HAL_OK : HAL_ERROR;
}

/******************************************************************************
 * u8_spiflash_write_enable()
 *
 * Send SPI FLASH write enable (WREN) command
 ******************************************************************************/

uint8_t u8_spiflash_write_enable(void)
{
    uint8_t u8_data;
    HAL_StatusTypeDef x_status;

    u8_data = MX_CMD_WREN;
    v_spiflash_select();
    x_status = HAL_SPI_Transmit(p_x_spiflash_hspi, &u8_data, 1, SPIFLASH_TIMEOUT);
    v_spiflash_deselect();

    return x_status;
}

/******************************************************************************
 * u8_spiflash_write_disable()
 *
 * Send SPI FLASH write disable (WRDI) command
 ******************************************************************************/

uint8_t u8_spiflash_write_disable(void)
{
    uint8_t u8_data;
    HAL_StatusTypeDef x_status;

    u8_data = MX_CMD_WRDI;
    v_spiflash_select();
    x_status = HAL_SPI_Transmit(p_x_spiflash_hspi, &u8_data, 1, SPIFLASH_TIMEOUT);
    v_spiflash_deselect();

    return x_status;
}

/******************************************************************************
 * u8_spiflash_powerdown()
 *
 * Send SPI FLASH Deep Power-down (DP) command
 *
 * Note:
 * After sending this command, the device will enter the deep powerdown state.
 * Asserting CS (low) and sending the Release from Deep Power Down command
 * will wake it up.
 * Minimum delay before attempting wake-up: 30 uS
 *   e.g. device must remain in powerdown mode for a minimum of 30 uS before
 *   attempting wake-up.
 ******************************************************************************/

uint8_t u8_spiflash_powerdown(void)
{
    uint8_t u8_data;
    HAL_StatusTypeDef x_status;

    u8_data = MX_CMD_DP;
    v_spiflash_select();
    x_status = HAL_SPI_Transmit(p_x_spiflash_hspi, &u8_data, 1, SPIFLASH_TIMEOUT);
    v_spiflash_deselect();

    return x_status;
}

/******************************************************************************
 * u8_spiflash_powerup()
 *
 * Send SPI FLASH Release from Deep Power-down (RDP) command
 *
 * Note:
 * After sending this command, the device will exit deep-power-down mode and
 * enter the normal idle power state.
 * Minimum delay after release from DP mode to normal operation: 30 uS
 *   e.g. device must remain in powerdown mode for a minimum of 30 uS before
 *   attempting wake-up.
 * Recovery time after CS rise after sending RDP command: 30 uS (tRES)
 *   After sending the Release from Deep Power Mode command and releasing CS,
 *   the device requires 30 uS of recovery time before it can process commands.
 ******************************************************************************/

uint8_t u8_spiflash_powerup(void)
{
    uint8_t u8_data;
    HAL_StatusTypeDef x_status;

    u8_data = MX_CMD_RDP;
    v_spiflash_select();
    x_status = HAL_SPI_Transmit(p_x_spiflash_hspi, &u8_data, 1, SPIFLASH_TIMEOUT);
    v_spiflash_deselect();

    return x_status;
}

/******************************************************************************
 * u8_spiflash_reset()
 *
 * Send SPI FLASH reset enable (RSTEN) followed by software reset (RST)
 * commands.
 *
 * Note:
 * It is recommended that no commands be sent to the device for at least 30 uS
 * after the software reset has been executed.
 ******************************************************************************/

uint8_t u8_spiflash_reset(void)
{
    uint8_t u8_data;
    HAL_StatusTypeDef x_status;

    do
    {
        u8_data = MX_CMD_RSTEN;
        v_spiflash_select();
        x_status = HAL_SPI_Transmit(p_x_spiflash_hspi, &u8_data, 1, SPIFLASH_TIMEOUT);
        v_spiflash_deselect();
        if (x_status != HAL_OK) break;

        u8_data = MX_CMD_RST;
        v_spiflash_select();
        x_status = HAL_SPI_Transmit(p_x_spiflash_hspi, &u8_data, 1, SPIFLASH_TIMEOUT);
    }
    while (0);

    v_spiflash_deselect();

    return x_status;
}

/******************************************************************************
 * u8_spiflash_read_id(*p_x_id)
 *
 * Read the SPI FLASH device information.
 *
 * *p_x_id      A spiflash_id_t struct pointer where the device information
 *              will be stored.
 *
 * Note:
 * The MX25R80 datasheet is unclear as to the interpretation of the
 * u8_memory_density value; for this device, it returns 20 (decimal).
 * This may be the power-of-2 number of bytes of storage available;
 * 2^20 = 1048576 (1MB) bytes
 ******************************************************************************/

uint8_t u8_spiflash_read_id(spiflash_id_t *p_x_id)
{
    uint8_t u8_data;
    HAL_StatusTypeDef x_status;

    if (p_x_id == NULL)
    {
        return HAL_ERROR;
    }
    memset(p_x_id, 0, sizeof(spiflash_id_t));

    do
    {
        v_spiflash_select();
        u8_data = MX_CMD_RDID2;     /* 0x9F: JEDEC-standard Read ID. 0x9E is a
                                     * Macronix-specific alias that W25Q/SST parts
                                     * do not answer -- use 0x9F for portability. */
        x_status = HAL_SPI_Transmit(p_x_spiflash_hspi, &u8_data, 1, SPIFLASH_TIMEOUT);
        if (x_status != HAL_OK) break;

        x_status = HAL_SPI_Receive(p_x_spiflash_hspi, (uint8_t *) p_x_id, 3, SPIFLASH_TIMEOUT);
    }
    while (0);

    v_spiflash_deselect();

    return (uint8_t) x_status;
}

/******************************************************************************
 * u8_spiflash_read_status(*p_x_status)
 *
 * Read SPI FLASH status register
 *
 * *p_x_status  Pointer to a spiflash_status_reg_t struct where the device
 *              status will be stored.
 ******************************************************************************/

uint8_t u8_spiflash_read_status(spiflash_status_reg_t *p_x_status)
{
    uint8_t u8_data;
    HAL_StatusTypeDef x_status;

    if (p_x_status == NULL)
    {
        return HAL_ERROR;
    }
    p_x_status->all = 0;

    do
    {
        v_spiflash_select();
        u8_data = MX_CMD_RDSR;
        x_status = HAL_SPI_Transmit(p_x_spiflash_hspi, &u8_data, 1, SPIFLASH_TIMEOUT);
        if (x_status != HAL_OK) break;

        x_status = HAL_SPI_Receive(p_x_spiflash_hspi, (uint8_t *) p_x_status, 1, SPIFLASH_TIMEOUT);
    }
    while (0);

    v_spiflash_deselect();

    return (uint8_t) x_status;
}

/******************************************************************************
 * u8_spiflash_write_wait(u16_timeout)
 *
 * Wait for SPI FLASH write or erase operation to complete
 *
 * u16_timeout  Number of milliseconds to wait for write/erase operation to
 *              complete.
 *
 * Repeatedly reads the SPI FLASH status register until the WIP (write-in-
 * progress) and WEL (write-enable latch) bits are cleared.
 * Will abort and return a timeout status if this does not happen within
 * <u16_timeout> milliseconds.
 ******************************************************************************/

uint8_t u8_spiflash_write_wait(uint16_t u16_timeout)
{
    uint32_t u32_timestamp;
    uint32_t u32_elapsed_time;
    uint8_t u8_data;
    spiflash_status_reg_t x_spiflash_status;
    HAL_StatusTypeDef x_status;

    v_spiflash_select();
    u8_data = MX_CMD_RDSR;
    x_status = HAL_SPI_Transmit(p_x_spiflash_hspi, &u8_data, 1, SPIFLASH_TIMEOUT);
    if (x_status != HAL_OK)
    {
        return x_status;
    }

    u32_timestamp = HAL_GetTick();
    do
    {
        x_spiflash_status.all = 0;
        // Note: After sending the RDSR command, the status register may be
        // repeatedly read without re-sending the RDSR command and toggling
        // the select line simply by clocking out (receiving) another 8
        // bits.
        x_status = HAL_SPI_Receive(p_x_spiflash_hspi, (uint8_t *) &x_spiflash_status,
                                   sizeof(spiflash_status_reg_t), 2);
        if (x_status != HAL_OK) break;

        u32_elapsed_time = HAL_GetTick() - u32_timestamp;
        if (u32_elapsed_time > u16_timeout)
        {
            x_status = HAL_TIMEOUT;
            break;
        }
    }
    // Exit when Write In Progress (wip) and Write Enable Latch (wel) are both clear
//  while (x_spiflash_status.wip || x_spiflash_status.wel);
    while (x_spiflash_status.wip);

    v_spiflash_deselect();

    return (uint8_t) x_status;
}

/******************************************************************************
 * u8_spiflash_read_config(*p_x_config)
 *
 * Read the SPI FLASH configuration register
 *
 * *p_x_config  Pointer to a <spiflash_config_reg_t> struct where the device
 *              status register contents will be stored.
 ******************************************************************************/

uint8_t u8_spiflash_read_config(spiflash_config_reg_t *p_x_config)
{
    uint8_t u8_data;
    HAL_StatusTypeDef x_status;

    if (p_x_config == NULL)
    {
        return HAL_ERROR;
    }
    p_x_config->all = 0;

    do
    {
        v_spiflash_select();
        u8_data = MX_CMD_RDCR;
        x_status = HAL_SPI_Transmit(p_x_spiflash_hspi, &u8_data, 1, SPIFLASH_TIMEOUT);
        if (x_status != HAL_OK) break;

        x_status = HAL_SPI_Receive(p_x_spiflash_hspi, (uint8_t *) p_x_config, sizeof(spiflash_config_reg_t), SPIFLASH_TIMEOUT);
    }
    while (0);

    v_spiflash_deselect();

    return (uint8_t) x_status;
}

/******************************************************************************
 * u8_spiflash_write_config(*p_x_status, *p_x_config)
 *
 * Write to the SPI FLASH status and configuration registers.
 *
 * *p_x_status  Pointer to a <spiflash_status_reg_t> struct containing the
 *              status register value to send to the device
 * *p_x_config  Pointer to a <spiflash_config_reg_t> struct containing the
 *              configuration data to send to the device
 *
 * Note:
 * This function will wait for previous device write operations to complete
 * (u8_spiflash_write_wait) and send the write enable command (u8_spiflash_
 * write_enable) before attempting to execute the write status/config (WRSR)
 * command.
 * Exercise caution when using this function. Some of the status and
 * configuration register bits are one-time programmable (OTP).
 ******************************************************************************/

uint8_t u8_spiflash_write_config(const spiflash_status_reg_t *p_x_status, const spiflash_config_reg_t *p_x_config)
{
    uint8_t u8_data[4];
    HAL_StatusTypeDef x_status;

    if ((p_x_status == NULL) || (p_x_config == NULL))
    {
        return HAL_ERROR;
    }

    do
    {
        x_status = u8_spiflash_write_wait(SPIFLASH_TIMEOUT);
        if (x_status != HAL_OK) break;

        x_status = u8_spiflash_write_enable();
        if (x_status != HAL_OK) break;

        u8_data[0] = MX_CMD_WRSR;
        u8_data[1] = p_x_status->all;
        u8_data[2] = p_x_config->cr1;
        u8_data[3] = p_x_config->cr2;

        v_spiflash_select();
        x_status = HAL_SPI_Transmit(p_x_spiflash_hspi, u8_data, sizeof(u8_data), SPIFLASH_TIMEOUT);
        if (x_status != HAL_OK) break;

        x_status = u8_spiflash_write_wait(SPIFLASH_TIMEOUT);
    }
    while (0);

    v_spiflash_deselect();

    return x_status;
}

/******************************************************************************
 * u8_spiflash_read_security_reg(*p_x_secreg)
 *
 * Read the SPI FLASH security status/configuration register
 ******************************************************************************/

uint8_t u8_spiflash_read_security_reg(spiflash_security_reg_t *p_x_secreg)
{
    uint8_t u8_data;
    HAL_StatusTypeDef x_status;

    if (p_x_secreg == NULL)
    {
        return HAL_ERROR;
    }
    p_x_secreg->all = 0;

    do
    {
        v_spiflash_select();
        u8_data = MX_CMD_RDSCUR;
        x_status = HAL_SPI_Transmit(p_x_spiflash_hspi, &u8_data, 1, SPIFLASH_TIMEOUT);
        if (x_status != HAL_OK) break;

        x_status = HAL_SPI_Receive(p_x_spiflash_hspi, (uint8_t *) p_x_secreg, sizeof(spiflash_security_reg_t), SPIFLASH_TIMEOUT);
    }
    while (0);

    v_spiflash_deselect();

    return (uint8_t) x_status;
}

/******************************************************************************
 * u8_spiflash_erase(u8_opcode, u32_address)
 *
 * Helper function for performing device erase operations, all of which have
 * the same command format with exception of the initial operation code byte.
 *
 * u8_opcode    Device operation code; normally one of the device erase commands
 *              such as SE, BE32K, BE
 * u8_address   Byte address of sector/block to be erased
 *
 * Note:
 * Prior write operations will be waited on, and the write enable mode will
 * be set before the erase command sequence is sent.
 ******************************************************************************/

static uint8_t u8_spiflash_erase(uint8_t u8_opcode, uint32_t u32_address)
{
    uint8_t u8_data[4];
    HAL_StatusTypeDef x_status;

    do
    {
        x_status = u8_spiflash_write_wait(SPIFLASH_TIMEOUT);
        if (x_status != HAL_OK) break;

        x_status = u8_spiflash_write_enable();
        if (x_status != HAL_OK) break;

        u8_data[0] = u8_opcode;
        u8_data[1] = (uint8_t) ((u32_address >> 16) & 0xFF);
        u8_data[2] = (uint8_t) ((u32_address >> 8) & 0xFF);
        u8_data[3] = (uint8_t) (u32_address & 0xFF);

        v_spiflash_select();
        x_status = HAL_SPI_Transmit(p_x_spiflash_hspi, u8_data, sizeof(u8_data), SPIFLASH_TIMEOUT);
        if (x_status != HAL_OK) break;
        v_spiflash_deselect();

        x_status = u8_spiflash_write_wait(SPIFLASH_TIMEOUT);
    }
    while (0);

    v_spiflash_deselect();

    return (uint8_t) x_status;
}

/******************************************************************************
 * u8_spiflash_sector_erase(u32_address)
 *
 * Erase a 4K sector
 *
 * u32_address  Byte address of the sector to be erased.
 *              May point to anywhere within the 4K sector.
 ******************************************************************************/

uint8_t u8_spiflash_sector_erase(uint32_t u32_address)
{
    uint8_t u8_status = u8_spiflash_erase(MX_CMD_SE, u32_address);
    return u8_status;
}

/******************************************************************************
 * u8_spiflash_32k_block_erase(u32_address)
 *
 * Erase a 32K block
 *
 * u32_address  Byte address of the 32K block region to be erased.
 *              May point to anywhere within the 32k block.
 ******************************************************************************/

uint8_t u8_spiflash_32k_block_erase(uint32_t u32_address)
{
    uint8_t u8_status = u8_spiflash_erase(MX_CMD_BE32K, u32_address);
    return u8_status;
}

/******************************************************************************
 * u8_spiflash_block_erase(u32_address)
 *
 * Erase a 64K block
 *
 * u32_address  Byte address of the 64K block to be erased.
 *              May point to anywhere within the 64K block.
 ******************************************************************************/

uint8_t u8_spiflash_block_erase(uint32_t u32_address)
{
    uint8_t u8_status = u8_spiflash_erase(MX_CMD_BE, u32_address);
    return u8_status;
}

/******************************************************************************
 * u8_spiflash_chip_erase()
 *
 * Erase the entire SPI FLASH memory array (not including OTP data)
 *
 * Note:
 * If any portion of the memory array is under block write protection, this
 * command will not be executed.
 ******************************************************************************/

uint8_t u8_spiflash_chip_erase(void)
{
    HAL_StatusTypeDef x_status;
    uint8_t u8_data;

    do
    {
        x_status = u8_spiflash_write_wait(SPIFLASH_TIMEOUT);
        if (x_status != HAL_OK) break;

        x_status = u8_spiflash_write_enable();
        if (x_status != HAL_OK) break;

        u8_data = MX_CMD_CE;
        v_spiflash_select();
        x_status = HAL_SPI_Transmit(p_x_spiflash_hspi, &u8_data, 1, SPIFLASH_TIMEOUT);
        v_spiflash_deselect();
        if (x_status != HAL_OK) break;

        // A full chip erase takes quite a bit of time
        x_status = u8_spiflash_write_wait(8000);
    }
    while (0);

    v_spiflash_deselect();

    return (uint8_t) x_status;
}

/******************************************************************************
 * u8_spiflash_read(*v_read_data, u32_address, u16_size)
 *
 * Read SPI FLASH memory
 *
 * *v_read_data Pointer to buffer where data read will be stored
 *              No alignment requirements, read may be started at any address
 * u32_address  Starting byte address to read
 * u16_size     Number of bytes to read
 ******************************************************************************/

uint8_t u8_spiflash_read(void *v_read_data, uint32_t u32_address, uint16_t u16_size)
{
    uint8_t u8_data[5];
    HAL_StatusTypeDef x_status;

    if (v_read_data == NULL)
    {
        return HAL_ERROR;
    }

    u8_data[0] = MX_CMD_FASTREAD;
    u8_data[1] = (uint8_t) ((u32_address >> 16) & 0xFF);
    u8_data[2] = (uint8_t) ((u32_address >> 8) & 0xFF);
    u8_data[3] = (uint8_t) (u32_address & 0xFF);
    u8_data[4] = 0;

    do
    {
        v_spiflash_select();
        x_status = HAL_SPI_Transmit(p_x_spiflash_hspi, u8_data, sizeof(u8_data), SPIFLASH_TIMEOUT);
        if (x_status != HAL_OK) break;

        x_status = HAL_SPI_Receive_DMA(p_x_spiflash_hspi, (uint8_t *) v_read_data, u16_size);
        if (x_status != HAL_OK) break;

        x_status = u8_spi_busy_wait(p_x_spiflash_hspi, SPIFLASH_TIMEOUT);
    }
    while (0);

    v_spiflash_deselect();

    return (uint8_t) x_status;
}

/******************************************************************************
 * u8_spiflash_write_page(*v_write_data, u32_address, u16_size)
 *
 * Write (up to) one 256-byte page of the SPI FLASH memory array
 *
 * *v_write_data    Pointer to buffer containing data to be written
 * u32_address      Starting byte address in SPI FLASH to write to.
 *                  This is normally aligned to a 256-byte page, although it
 *                  is not a hard requirement.
 * u16_size         Number of bytes to write
 *                  This is normally set to 256 if the write starts on a page
 *                  boundary. It may be set to 0 to have the function calculate
 *                  the write size based on the <u32_address> passed in.
 *                  u16_size will be reduced to a value no greater than the
 *                  page size (256) if it is set to a larger value.
 *
 * Note:
 * Refer to MX25R80 datasheet section covering the Page Program (PP) command
 * for understanding the ramifications of using this command when the start
 * address (u32_address) is not page-aligned and/or write size (u16_size) is
 * not equal to the page size (256).
 ******************************************************************************/

uint8_t u8_spiflash_write_page(void *v_write_data, uint32_t u32_address, uint16_t u16_size)
{
    uint8_t u8_data[4];
    HAL_StatusTypeDef x_status;

    if (v_write_data == NULL)
    {
        return HAL_ERROR;
    }

    if (u16_size == 0)
    {
        u16_size = SPIFLASH_PAGE_SIZE - (u32_address & (SPIFLASH_PAGE_SIZE - 1));
    }
    if (u16_size > SPIFLASH_PAGE_SIZE)
    {
        u16_size = SPIFLASH_PAGE_SIZE;
    }

    do
    {
        x_status = u8_spiflash_write_wait(SPIFLASH_TIMEOUT);
        if (x_status != HAL_OK) break;

        x_status = u8_spiflash_write_enable();
        if (x_status != HAL_OK) break;
#if 0
spiflash_status_reg_t x_sstat;
u8_spiflash_read_status(&x_sstat);
printf("FStatus:%02X\r\n", x_sstat.all);
#endif
        u8_data[0] = MX_CMD_PP;
        u8_data[1] = (uint8_t) ((u32_address >> 16) & 0xFF);
        u8_data[2] = (uint8_t) ((u32_address >> 8) & 0xFF);
        u8_data[3] = (uint8_t) (u32_address & 0xFF);

        v_spiflash_select();
        x_status = HAL_SPI_Transmit(p_x_spiflash_hspi, u8_data, sizeof(u8_data), SPIFLASH_TIMEOUT);
        if (x_status != HAL_OK) break;

        x_status = HAL_SPI_Transmit_DMA(p_x_spiflash_hspi, (uint8_t *) v_write_data, u16_size);
        if (x_status != HAL_OK) break;
        x_status = u8_spi_busy_wait(p_x_spiflash_hspi, SPIFLASH_TIMEOUT);
        if (x_status != HAL_OK) break;
        v_spiflash_deselect();

        x_status = u8_spiflash_write_wait(SPIFLASH_TIMEOUT);
        if (x_status != HAL_OK) break;
    }
    while (0);

    v_spiflash_deselect();

    return (uint8_t) x_status;
}

/******************************************************************************
 * u8_spiflash_write(*v_write_data, u32_address, u16_size, u8_erase_option)
 *
 * General-purpose SPIFLASH data write.
 * Can be used to write a block of data up to 64K in size starting at any
 * valid SPIFLASH address.
 *
 * *v_write_data    Pointer to buffer containing data to be written
 * u32_address      Starting byte address in SPI FLASH to write to.
 *                  No alignment requirements - may be any valid SPIFLASH
 *                  address.
 * u16_size         Number of bytes to write
 *                  May be any size from 0 to 64K-1 bytes
 * u8_erase_option  If nonzero, SPIFLASH sectors being written to will be erased
 *                  before they are written. Set to 0 to disable erase-before-
 *                  write.
 *
 * Notes:
 * SPIFLASH sectors are erased when the u8_erase_opttion is enabled AND the
 * 'current address pointer' (the address where the next page-sized chunk will
 * be written to) lies directly on a sector boundary. If a write operation does
 * not start on a sector boundary, the starting sector will NOT be erased, but
 * all subsequent sectors that are written into will be.
 *
 ******************************************************************************/

uint8_t u8_spiflash_write(void *v_write_data, uint32_t u32_address, uint16_t u16_size, uint8_t u8_erase_option)
{
    uint32_t u32_current_address;
    uint16_t u16_page_write_size;
    uint8_t u8_status = 0;

    u32_current_address = u32_address;

    while (u16_size > 0)
    {
        // If current write address is at the start of a sector boundary, and
        // the erase option is enabled, then erase the sector about to be
        // written
        if ( ((u32_current_address & (SPIFLASH_SECTOR_SIZE - 1)) == 0) &&
             u8_erase_option )
        {
            u8_status = u8_spiflash_sector_erase(u32_current_address);
            if (u8_status) break;
        }

        // Write operations are limited to one page (256 byte) chunks - or less, if
        // write address does not start on a page boundary.
        // If the write operation does not start on a page boundary, then the
        // write chunk size is limited to (page size - address offset within
        // the page).
        // Example: If write base address is 0x008140, then:
        // Offset within page: 0x40 (nearest page boundary that is <= write
        //     base address is 0x008100)
        // Write chunk size: 0xC0 (page size (0x100) - offset (0x40))
        u16_page_write_size = SPIFLASH_PAGE_SIZE - (u32_current_address & (SPIFLASH_PAGE_SIZE - 1));
        if (u16_page_write_size > u16_size) u16_page_write_size = u16_size;

        // Write out (up to) one SPIFLASH page of data
        u8_status = u8_spiflash_write_page(v_write_data, u32_current_address, u16_page_write_size);
        if (u8_status) break;

        // Update write address, data size remaining, and data pointers
        u32_current_address += u16_page_write_size;
        u16_size -= u16_page_write_size;
        v_write_data = (uint8_t *) v_write_data + u16_page_write_size;
    }

    return u8_status;
}

