
struct highpoint_raid_conf
{
       int8_t  filler1[32];
       u_int32_t       magic;
#define HPT_MAGIC_OK   0x5a7816f0
#define HPT_MAGIC_BAD  0x5a7816fd  

       u_int32_t       magic_0;
       u_int32_t       magic_1;
       u_int32_t       order;  
#define HPT_O_MIRROR   0x01  
#define HPT_O_STRIPE   0x02
#define HPT_O_OK       0x04

       u_int8_t        raid_disks;
       u_int8_t        raid0_shift; 
       u_int8_t        type;
#define HPT_T_RAID_0   0x00 
#define HPT_T_RAID_1   0x01
#define HPT_T_RAID_01_RAID_0   0x02
#define HPT_T_SPAN             0x03
#define HPT_T_RAID_3           0x04   
#define HPT_T_RAID_5           0x05
#define HPT_T_SINGLEDISK       0x06
#define HPT_T_RAID_01_RAID_1   0x07

       u_int8_t        disk_number;
       u_int32_t       total_secs; 
       u_int32_t       disk_mode;  
       u_int32_t       boot_mode;
       u_int8_t        boot_disk; 
       u_int8_t        boot_protect;
       u_int8_t        error_log_entries;
       u_int8_t        error_log_index;  
       struct
       {
               u_int32_t       timestamp;
               u_int8_t        reason;   
#define HPT_R_REMOVED          0xfe      
#define HPT_R_BROKEN           0xff      

               u_int8_t        disk;
               u_int8_t        status;
               u_int8_t        sectors;
               u_int32_t       lba;
       } errorlog[32];
       u_int8_t        filler[60];
};
