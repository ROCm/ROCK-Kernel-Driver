/*
 *  super.h
 *  Header file for super.c
 *
 *  Copyright (C) 1995-1997 Martin von Löwis
 *  Copyright (C) 1996-1997 Régis Duchesne
 */

#define ALLOC_REQUIRE_LOCATION 1
#define ALLOC_REQUIRE_SIZE     2

int ntfs_get_free_cluster_count(ntfs_inode *bitmap);
int ntfs_get_volumesize(ntfs_volume *vol, ntfs_u64 *vol_size );
int ntfs_init_volume(ntfs_volume *vol,char *boot);
int ntfs_load_special_files(ntfs_volume *vol);
int ntfs_release_volume(ntfs_volume *vol);
void ntfs_insert_fixups(unsigned char *rec, int secsize);
int ntfs_fixup_record(ntfs_volume *vol, char *record, char *magic, int size);
int ntfs_allocate_clusters(ntfs_volume *vol, ntfs_cluster_t *location, int *count,
  int flags);
int ntfs_deallocate_clusters(ntfs_volume *vol, ntfs_cluster_t location, int count);
