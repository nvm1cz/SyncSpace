#ifndef SYNCSPACE_TRANSFER_H
#define SYNCSPACE_TRANSFER_H

#include <stddef.h>

#define TRANSFER_CHUNK_SIZE 65536

/*
 * Validate a single-level file name in group root:
 * - 1..64 characters in [A-Za-z0-9._-]
 * - Cannot be "." or ".."
 * - Cannot end with ".part" (reserved for temporary upload files)
 * Returns 1 if valid, 0 if invalid.
 */
int transfer_validate_filename(const char *name);

/*
 * Build physical destination and temporary (.part) paths inside
 * storage/groups/<group_id>/.
 * Returns 0 on success, SS_ERR_INVALID on invalid group_id or filename.
 */
int transfer_build_paths(int group_id, const char *name,
                         char *dest_path, size_t dest_cap,
                         char *part_path, size_t part_cap);

/*
 * Prepare an exclusive temporary upload file (<name>.part):
 * - Verifies group root exists and is a real directory (not a symlink).
 * - Verifies final destination does not already exist (no overwrite, no symlink).
 * - Creates and opens <name>.part with O_CREAT | O_EXCL | O_WRONLY.
 * Returns 0 on success (sets *fd_out and fills part_path/dest_path),
 * or SS_ERR_INVALID / SS_ERR_EXISTS / SS_ERR_IO on failure.
 */
int transfer_open_temp(int group_id, const char *name,
                       int *fd_out,
                       char *dest_path, size_t dest_cap,
                       char *part_path, size_t part_cap);

/*
 * Atomically publish a completed temporary file to dest_path without
 * overwriting any existing file (via link() + unlink()).
 * Always removes part_path even on failure.
 * Returns 0 on success, SS_ERR_EXISTS if dest_path already exists,
 * or SS_ERR_IO on error.
 */
int transfer_commit_temp(const char *part_path, const char *dest_path);

/*
 * Abort an in-progress upload: close fd (if >= 0) and unlink part_path.
 */
void transfer_abort_temp(int fd, const char *part_path);

/*
 * Safely open an existing regular file for download:
 * - Rejects invalid filenames, symlinks, directories, or special files.
 * - Uses lstat() + open(O_RDONLY | O_NOFOLLOW) + fstat().
 * Returns 0 on success (sets *fd_out and *size_out),
 * SS_ERR_INVALID on invalid parameters/path or non-regular file/symlink,
 * SS_ERR_IO if file does not exist or cannot be opened.
 */
int transfer_open_download(int group_id, const char *name,
                           int *fd_out, unsigned long long *size_out);

/*
 * Receive exactly `size` raw bytes from `sock` in chunks of up to 64 KiB
 * and write them all into `fd`.
 * Returns 0 on success, -1 on socket EOF/error, SS_ERR_IO on local disk write error.
 */
int transfer_recv_to_fd(int sock, int fd, unsigned long long size);

/*
 * Read exactly `size` raw bytes from `fd` in chunks of up to 64 KiB
 * and send them all to `sock` via send_all().
 * Returns 0 on success, -1 on socket error or unexpected disk read/EOF error.
 */
int transfer_send_from_fd(int sock, int fd, unsigned long long size);

#endif
