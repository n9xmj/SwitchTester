/******************************************************************************
 * nvmparams.h
 *
 * Non-volatile memory parameter management
 ******************************************************************************/

/******************************************************************************
Notes concerning the NVM API and the data structures used by it

- Application is expected to declare an instance of the nvm_pool_t structure.
  This serves as a 'handle' that most NVM API functions need.
  The application code does not have to initialize the members of this struct
  directly; this is done by calling x_nvm_pool_init().
- The application can -optionally- allocate the RAM memory pool in which data
  objects destined to be read/written from NVM are stored. x_nvm_pool_init()
  can also do this; it will make use of malloc() to create this buffer.
- The format of the data stored in the NVM RAM memory pool is:
  + Header info (nvm_header_t) - data signature, data CRC, write count
  + One or more instances of nvm_object_t. Each object contains a 16-bit ID,
    16-bit data field size, and a data field which occupies a minimum of
    4 bytes (even if the object stored in is smaller) and grows in 4-byte
    increments. The <u32_size> field will always reflect the sizeof() the
    object stored in the data field, even if it is less than the amount of
    memory allocated for it.
  + An end record - this is another nvm_object_t, with ID set to
    NVM_PARAM_END_OF_DATA (0xFFFF) and u16_size set to 0, with a zero-length
    data field.

Usage notes:

- Prerequisite 1: Create device read and write handlers for the non-volatile
  storage device(s) to be used with this API.
  A driver for the STM32G0 series internal FLASH - NVM_DEVICE_MCUFLASH -
  has already been created. This driver will likely work with many other
  STM32 variants, but may require some changes. Make sure that the linker
  *.ld configuration file is modified to reserve the STM32 FLASH sector
  that will be used for NVM storage. See the STM32G030K8TX_FLASH.ld file for
  the ESprayer project for guidance.
  If using a different discrete NVM device, such as a SPI or I2C EEPROM,
  a custom R/W driver will have to be coded for it.
- Prerequisite 2: Define enum names for the objects you wish to store
  in NVM. These should be added to the definition of the nvm_param_id_t enum,
  which is declared in nvmparams.h. Refer to comments and examples there
  for additional guidance.

- Declare instance of nvm_pool_t:
    nvm_pool_t x_nvm_pool;
- Initialize it (allocate space, read from NVM device)
    This example requests x_nvm_pool_init to allocate the pool RAM buffer
    (using malloc()) by passing NULL as the 3rd parameter. Make sure that
    sufficient heap space (as set in the application *.ld linker config
    file) has been allocated if using this method.
    If using a application-allocated buffer, pass a pointer to it as the 3rd
    parameter.
    x_nvm_pool_init will fill the RAM buffer with the content stored in the NVM
    device. If it has not been stored or 'formatted' previously, it will be
    done here.

    Example:
    x_nvm_pool_init(&x_nvm_pool, NVM_DEVICE_MCU_FLASH, NULL, NVM_DATA_POOL_SIZE);

    The nvmparams API pre-declares a pool control record <g_x_nvm_param>, and a
    MCU FLASH buffer <nvm_mcu_flash> which can be used by the application.
    The application still needs to initialize this using x_nvm_pool_init, as
    shown in the example call above (replace x_nvm_pool with g_x_nvm_param).
    <nvm_mcu_flash> is referenced internally in the API by the x_mcuflash_read()
    and x_mcuflash_write() device drivers in nvmparams.c and will be allocated
    and used without additional user code as long as the linker .ld file has been
    properly modified.
- Make one or more calls to x_nvm_create() to create NVM storage objects and set
  their default values IF they do not already exist. x_nvm_create() will not
  modify or change objects that already exist.
  Example:
  uint16_t u16_nvm_parameter = 12345;
  x_nvm_create(&x_nvm_pool, NVM_PARAM_MYPARAM, sizeof(u16_nvm_parameter), &u16_nvm_parameter);
- Use x_nvm_get() to read existing objects out of the NVM storage pool, and
  x_nvm_set() to change the value; e.g.
  x_nvm_get(&x_nvm_pool, NVM_PARAM_MYPARAM, &u16_nvm_parameter);
    - Reads value associated with the NVM_PARAM_MYPARAM data object.
  x_nvm_set(&x_nvm_pool, NVM_PARAM_MYPARAM, &u16_nvm_parameter);
    - Writes value stored in <u16_nvm_parameter> to the NVM_PARAM_MYPARAM object.
- To get updated values set by x_nvm_set() written out to the physical storage
  device, call x_nvm_commit(&x_nvm_pool).
  A separate commit call is used instead of immediately writing out changes
  when x_nvm_set is called for several reasons:
  - Since NVM write operations often take a significant amount of time to
    complete, and will block execution while doing so, NVM write operations
    should be done outside of interrupt context or time-critical sections of
    code. Using a seperate commit step allows the physical write operation to
    take place at the time of the user's choosing.
  - To minimize the number of write cycles (wear) on the NVM storage device,
    it is possible to perform multiple x_nvm_set() calls then write out all the
    changed values in a single write operation using x_nvm_commit()
  - The commit operation will only perform a physical write if one or more NVM
    objects have been changed since the previous invocation of x_nvm_commit().
    Sufficient 'intelligence' has been coded into x_nvm_set() and x_nvm_commit()
    to minimize NVM write cycles.
  The downside of this is that data objects updated using x_nvm_set(), as well
  as new NVM objects initialized using x_nvm_create() or objects deleted from
  the pool using x_nvm_delete() will not 'stick' (be written to NVM) until
  x_nvm_commit() is called.
- x_nvm_delete() can be used to permanently remove data objects that are no
  longer needed. x_nvm_delete() will perform a simple form of "garbage
  collection" to ensure that pool space used by the deleted object is recovered
  and usable by new objects created.
- Recommended process for erasing an existing NVM pool and forcing defaults
  to be loaded:
    memset(x_nvm_pool.p_v_data, 0xFF, x_nvm_pool.u32_size);
    x_nvm_write(&x_nvm_pool);
    HAL_NVIC_SystemReset();
- Almost all x_nvm_*() functions will return with a standardized result/error
  code taken from the nvm_error_t enum.
*******************************************************************************/

#ifndef NVMPARAMS_H
#define NVMPARAMS_H

// Default NVM buffer (pool) size (bytes)
#define NVM_POOL_SIZE_DEFAULT       0x200

// NVM data valid signature value
#define NVM_DATA_SIGNATURE          0x5AA5A55A

// Max length of NVM pool label
#define NVM_LABEL_MAX_LENGTH        16

// This is a list of device IDs that can be used for storing NVM data pools.
// These get passed to the application-specific implementations of
// i_nvm_read() and i_nvm_write() to identify the nonvolatile storage
// device/file/etc. that should be accessed. It is up to the API user to
// translate this ID into code that accesses the target's nonvolatile
// storage device(s), device addresses, file names, or whatever.

typedef enum PACKED
{
    // NVM_DEVICE_NONE is reserved for the null device
    // When this device is associated with a memory pool, the pool RAM buffer
    // will be used/updated as normal, but data in the pool will not be read
    // from or written to a physical NVM storage device.
    NVM_DEVICE_NONE = 0,

    // These device IDs are project-specific
    // Add, remove or change them as needed.
    // Each of these device ID's needs an implementation coded in x_nvm_read()
    // and x_nvm_write().
    NVM_DEVICE_MCUFLASH,

    // Device ID for filesystem based NVM device implementation
    // Not used in this project - shown here to illustrate one of the
    // things that need to be done to add a NVM storage device.
    // Refer to commented-out code in x_nvm_read() and x_nvm_write() in
    // the NVM_DEVICE_FILE case.
    NVM_DEVICE_FILE,

    NVM_DEVICE_MAXVAL = 0xFF
}
nvm_device_t;

// NVM parameter IDs
// Use these enum labels to specify the object ID (x_id) that is one of the
// expected parameters for most nvmparams API calls.
//
// NOTE: NVM_PARAM_UNUSED and NVM_PARAM_END_OF_DATA are used internally
// by the API code. Do not use these IDs for user parameters.
//
// If multiple NVM pools and devices are in use, it is permissible to have
// multiple enum lists like the one below to track object IDs, one list/enum
// per pool. It is also permissible to use a single parameter ID list that is
// used for all pools. In some ways the single list approach is preferred,
// since attempting to get/set an object using the wrong pool will generate
// a (runtime) error that the application can check/detect.

typedef enum PACKED
{
    // Marks a NVM data block (object) that has been deleted or is otherwise unused
    NVM_PARAM_UNUSED = 0,

    // Factory device configuration parameters
    // These are typically things that are set once during manufacturing and
    // (usually) never changed once the product is shipped.
    // TODO: NVM_CONFIG_* - Some of these IDs may be unused or changed later.
    NVM_CONFIG_SERIAL_NUMBER,
    NVM_CONFIG_PRODUCT_ID,
    NVM_CONFIG_SKU,

    // NVM parameter ID's start here
    // Used with pool g_x_nvm_param
    NVM_PARAM_BASE_ID = 0xFF,           // IDs 0..255 reserved for CONFIG parameters

    // System parameters
    // New parameters should be added to the end of this list.

    // Switch outputs - manual pulse width, milliseconds
    NVM_PARAM_SWITCH_PULSE_MS,          // 0x100

    // Switch cycling parameters, three per channel.
    // These MUST remain contiguous and in this order: the ID for a given
    // (channel, parameter) pair is computed arithmetically as
    //     NVM_PARAM_CYCLE_A_REPEAT + (channel * SWITCH_CYCLE_PARAM_COUNT) + parameter
    // See x_switch_cycle_nvm_id() in switch_out.c, which guards the assumption
    // with _Static_assert.
    NVM_PARAM_CYCLE_A_REPEAT,           // 0x101
    NVM_PARAM_CYCLE_A_ON_US,
    NVM_PARAM_CYCLE_A_OFF_US,
    NVM_PARAM_CYCLE_B_REPEAT,
    NVM_PARAM_CYCLE_B_ON_US,
    NVM_PARAM_CYCLE_B_OFF_US,
    NVM_PARAM_CYCLE_C_REPEAT,
    NVM_PARAM_CYCLE_C_ON_US,
    NVM_PARAM_CYCLE_C_OFF_US,
    NVM_PARAM_CYCLE_D_REPEAT,
    NVM_PARAM_CYCLE_D_ON_US,
    NVM_PARAM_CYCLE_D_OFF_US,           // 0x10C

    // Parameters used for test/debug
    NVM_PARAM_TEST_1 = 0xFFF0,
    NVM_PARAM_TEST_2 = 0xFFF1,
    NVM_PARAM_TEST_3 = 0xFFF2,

    // Marks the end of NVM data space in use
    // The last parameter block in the data space will always be
    // marked with this ID.
    NVM_PARAM_END_OF_DATA = 0xFFFF
}
nvm_param_id_t;

// NVM API error codes
//
// NVM API errors are generally negative values.
// Positive values are reserved for device-specific errors

typedef enum PACKED
{
    NVM_ERROR_NONE          = 0,    // No error / OK
    NVM_ERROR_PARAMETER     = -1,   // Invalid function parameter or internal state error
    NVM_ERROR_MEMORY        = -2,   // Memory allocation error
    NVM_ERROR_DEVICE        = -3,   // Invalid device, or generic device error
    NVM_ERROR_ID_NOT_FOUND  = -4,   // Requested object ID does not exist
    NVM_ERROR_OBJECT_EXISTS = -5,   // Object already exists in memory pool (returned by x_nvm_create; not really an error)
    NVM_ERROR_OBJECT_NOT_FOUND = -6, // Requested object not found in memory pool
    NVM_ERROR_POOL_CORRUPT  = -7,   // The NVM data pool is corrupt or not properly formatted

    NVM_ERROR_MINVAL        = -32768,
    NVM_ERROR_MAXVAL        = 32767
}
nvm_error_t;

// NVM pool control structure
// This contains the data needed to manage a given NVM data pool; it does not
// contain the stored data itself

typedef struct
{
    nvm_device_t    x_device;           // NVM device ID associated with this pool
    uint8_t         u8_need_commit;     // Set: Content modified - commit needed
    uint8_t         u8_internal_malloc; // Set: p_v_data pool malloc()'d by x_nvm_pool_init()
    uint8_t         u8_user1;           // User code may use this for any purpose
    uint16_t        u16_commit_timer;   // May be used in user code to implement an auto-commit timer.
                                        //   Reset to 0 when x_nvm_commit() is called.
    uint16_t        u16_user2;          // User code may use this for any purpose
    uint32_t        u32_size;           // Size of memory pool (bytes)
    void            *p_v_data;          // Pointer to RAM memory pool
}
nvm_pool_t;

// NVM data pool header object

typedef struct
{
    uint32_t u32_signature;
    uint32_t u32_crc;
    uint32_t u32_write_count;
    char c_label[NVM_LABEL_MAX_LENGTH];
}
nvm_header_t;

// NVM data object record
// Each object stored in the NVM data pool uses this format

typedef struct
{
    nvm_param_id_t x_id;                // NVM data object identifier; its "file name"
    uint16_t u16_size;                  // Actual size of object (does not have to be a multiple of 4)
    // The size of u8_data[] that will be allocated in the NVM data buffer is
    // equal to (u16_size + 3) & 0xFFFC; i.e. 4-byte increments
    uint8_t u8_data[0];                 // NVM parameter/object data - variable length
}
nvm_object_t;

//------------------------------------------------------------------------------

// NVM pools and data used by the project
// These items are project-specific; it is OK to delete, rename or re-use them
// in a different project.

//extern nvm_pool_t g_x_nvm_config;
extern nvm_pool_t g_x_nvm_param;

//------------------------------------------------------------------------------

extern nvm_error_t x_nvm_pool_init(nvm_pool_t *p_x_pool, nvm_device_t x_device,
                                   void *p_v_ram_buffer, uint32_t u32_size,
                                   const char *p_c_label);
extern nvm_error_t x_nvm_pool_release(nvm_pool_t *p_x_pool);
extern nvm_error_t x_nvm_commit(nvm_pool_t *p_x_pool);
extern nvm_error_t x_nvm_create(nvm_pool_t *p_x_pool, nvm_param_id_t x_id,
                                uint16_t u16_size, const void *p_v_default);
extern nvm_error_t x_nvm_delete(nvm_pool_t *p_x_pool, nvm_param_id_t x_id);
extern nvm_error_t x_nvm_get(nvm_pool_t *p_x_pool, nvm_param_id_t x_id, void *p_v_data);
extern nvm_error_t x_nvm_get_size(nvm_pool_t *p_x_pool, nvm_param_id_t x_id, uint16_t *p_u16_size);
extern nvm_error_t x_nvm_set(nvm_pool_t *p_x_pool, nvm_param_id_t x_id, const void *p_v_data);

extern nvm_error_t x_nvm_list(nvm_pool_t *p_x_pool);

// Use of the function(s) below in application code is not recommended.

extern nvm_object_t * p_x_nvm_search(nvm_pool_t *p_x_pool, nvm_param_id_t x_id);
extern nvm_error_t x_nvm_read(nvm_pool_t *p_x_pool);
extern nvm_error_t x_nvm_write(nvm_pool_t *p_x_pool);

#endif
