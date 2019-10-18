#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/param.h>

#include <mach-o/loader.h>
#include <mach-o/fat.h>

//Added by us
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>


#define IS_64_BIT(x) ((x) == MH_MAGIC_64 || (x) == MH_CIGAM_64)
#define IS_LITTLE_ENDIAN(x) ((x) == FAT_CIGAM || (x) == MH_CIGAM_64 || (x) == MH_CIGAM)
#define SWAP32(x, magic) (IS_LITTLE_ENDIAN(magic)? OSSwapInt32(x): (x))
#define SWAP64(x, magic) (IS_LITTLE_ENDIAN(magic)? OSSwapInt64(x): (x))

#define ROUND_UP(x, y) (((x) + (y) - 1) & -(y))

#define ABSDIFF(x, y) ((x) > (y)? (uintmax_t)(x) - (uintmax_t)(y): (uintmax_t)(y) - (uintmax_t)(x))

#define BUFSIZE 512

void fbzero(FILE *f, off_t offset, size_t len) {
	static unsigned char zeros[BUFSIZE] = {0};
	fseeko(f, offset, SEEK_SET);
	while(len != 0) {
		size_t size = MIN(len, sizeof(zeros));
		fwrite(zeros, size, 1, f);
		len -= size;
	}
}

void fmemmove(FILE *f, off_t dst, off_t src, size_t len) {
	static unsigned char buf[BUFSIZE];
	while(len != 0) {
		size_t size = MIN(len, sizeof(buf));
		fseeko(f, src, SEEK_SET);
		fread(&buf, size, 1, f);
		fseeko(f, dst, SEEK_SET);
		fwrite(buf, size, 1, f);

		len -= size;
		src += size;
		dst += size;
	}
}

size_t fpeek(void *restrict ptr, size_t size, size_t nitems, FILE *restrict stream) {
	off_t pos = ftello(stream);
	size_t result = fread(ptr, size, nitems, stream);
	fseeko(stream, pos, SEEK_SET);
	return result;
}

void *read_load_command(FILE *f, uint32_t cmdsize) {
	void *lc = malloc(cmdsize);

	fpeek(lc, cmdsize, 1, f);

	return lc;
}

bool check_load_commands(FILE *f, struct mach_header *mh, size_t header_offset, size_t commands_offset, off_t *slice_size) {
	fseeko(f, commands_offset, SEEK_SET);

	uint32_t ncmds = SWAP32(mh->ncmds, mh->magic);

	off_t linkedit_32_pos = -1;
	off_t linkedit_64_pos = -1;
	struct segment_command linkedit_32;
	struct segment_command_64 linkedit_64;

	off_t symtab_pos = -1;
	uint32_t symtab_size = 0;

	for(int i = 0; i < ncmds; i++) {
		struct load_command lc;
		fpeek(&lc, sizeof(lc), 1, f);

		uint32_t cmdsize = SWAP32(lc.cmdsize, mh->magic);
		uint32_t cmd = SWAP32(lc.cmd, mh->magic);

		switch(cmd) {
			case LC_CODE_SIGNATURE:
				if(i == ncmds - 1) {

					struct linkedit_data_command *cmd = read_load_command(f, cmdsize);

					fbzero(f, ftello(f), cmdsize);

					uint32_t dataoff = SWAP32(cmd->dataoff, mh->magic);
					uint32_t datasize = SWAP32(cmd->datasize, mh->magic);

					free(cmd);

					uint64_t linkedit_fileoff = 0;
					uint64_t linkedit_filesize = 0;

					if(linkedit_32_pos != -1) {
						linkedit_fileoff = SWAP32(linkedit_32.fileoff, mh->magic);
						linkedit_filesize = SWAP32(linkedit_32.filesize, mh->magic);
					} else if(linkedit_64_pos != -1) {
						linkedit_fileoff = SWAP64(linkedit_64.fileoff, mh->magic);
						linkedit_filesize = SWAP64(linkedit_64.filesize, mh->magic);
					} else {
						fprintf(stderr, "Warning: __LINKEDIT segment not found.\n");
					}

					if(linkedit_32_pos != -1 || linkedit_64_pos != -1) {
						if(linkedit_fileoff + linkedit_filesize != *slice_size) {
							fprintf(stderr, "Warning: __LINKEDIT segment is not at the end of the file, so codesign will not work on the patched binary.\n");
						} else {
							if(dataoff + datasize != *slice_size) {
								fprintf(stderr, "Warning: Codesignature is not at the end of __LINKEDIT segment, so codesign will not work on the patched binary.\n");
							} else {
								*slice_size -= datasize;
								//int64_t diff_size = 0;
								if(symtab_pos == -1) {
									fprintf(stderr, "Warning: LC_SYMTAB load command not found. codesign might not work on the patched binary.\n");
								} else {
									fseeko(f, symtab_pos, SEEK_SET);
									struct symtab_command *symtab = read_load_command(f, symtab_size);

									uint32_t strsize = SWAP32(symtab->strsize, mh->magic);
									int64_t diff_size = SWAP32(symtab->stroff, mh->magic) + strsize - (int64_t)*slice_size;
									if(-0x10 <= diff_size && diff_size <= 0) {
										symtab->strsize = SWAP32((uint32_t)(strsize - diff_size), mh->magic);
										fwrite(symtab, symtab_size, 1, f);
									} else {
										fprintf(stderr, "Warning: String table doesn't appear right before code signature. codesign might not work on the patched binary. (0x%llx)\n", diff_size);
									}

									free(symtab);
								}

								linkedit_filesize -= datasize;
								uint64_t linkedit_vmsize = ROUND_UP(linkedit_filesize, 0x1000);

								if(linkedit_32_pos != -1) {
									linkedit_32.filesize = SWAP32((uint32_t)linkedit_filesize, mh->magic);
									linkedit_32.vmsize = SWAP32((uint32_t)linkedit_vmsize, mh->magic);

									fseeko(f, linkedit_32_pos, SEEK_SET);
									fwrite(&linkedit_32, sizeof(linkedit_32), 1, f);
								} else {
									linkedit_64.filesize = SWAP64(linkedit_filesize, mh->magic);
									linkedit_64.vmsize = SWAP64(linkedit_vmsize, mh->magic);

									fseeko(f, linkedit_64_pos, SEEK_SET);
									fwrite(&linkedit_64, sizeof(linkedit_64), 1, f);
								}

								goto fix_header;
							}
						}
					}

					// If we haven't truncated the file, zero out the code signature
					fbzero(f, header_offset + dataoff, datasize);

				fix_header:
					printf("Fixing header for arch\n");
					mh->ncmds = SWAP32(ncmds - 1, mh->magic);
					mh->sizeofcmds = SWAP32(SWAP32(mh->sizeofcmds, mh->magic) - cmdsize, mh->magic);

					return true;
				} else {
					printf("LC_CODE_SIGNATURE is not the last load command, so couldn't remove.\n");
				}
				break;
			case LC_LOAD_DYLIB:
			case LC_LOAD_WEAK_DYLIB: {
				break;
			}
			case LC_SEGMENT:
			case LC_SEGMENT_64:
				if(cmd == LC_SEGMENT) {
					struct segment_command *cmd = read_load_command(f, cmdsize);
					if(strcmp(cmd->segname, "__LINKEDIT") == 0) {
						linkedit_32_pos = ftello(f);
						linkedit_32 = *cmd;
					}
					free(cmd);
				} else {
					struct segment_command_64 *cmd = read_load_command(f, cmdsize);
					if(strcmp(cmd->segname, "__LINKEDIT") == 0) {
						linkedit_64_pos = ftello(f);
						linkedit_64 = *cmd;
					}
					free(cmd);
				}
			case LC_SYMTAB:
				symtab_pos = ftello(f);
				symtab_size = cmdsize;
		}

		fseeko(f, SWAP32(lc.cmdsize, mh->magic), SEEK_CUR);
	}

	return true;
}

bool strip_signature(FILE *f, size_t header_offset, off_t *slice_size) {
	fseeko(f, header_offset, SEEK_SET);

	struct mach_header mh;
	fread(&mh, sizeof(struct mach_header), 1, f);

	if(mh.magic != MH_MAGIC_64 && mh.magic != MH_CIGAM_64 && mh.magic != MH_MAGIC && mh.magic != MH_CIGAM) {
		printf("Unknown magic: 0x%x\n", mh.magic);
		return false;
	}

	size_t commands_offset = header_offset + (IS_64_BIT(mh.magic)? sizeof(struct mach_header_64): sizeof(struct mach_header));

	bool cont = check_load_commands(f, &mh, header_offset, commands_offset, slice_size);

	if(!cont) {
		return true;
	}
	fseeko(f, header_offset, SEEK_SET);
	fwrite(&mh, sizeof(mh), 1, f);
	return true;
}

int main(int argc, const char *argv[]) {

	const char *binary_path = argv[1];

	struct stat s;

	if(stat(binary_path, &s) != 0) {
		perror(binary_path);
		exit(1);
	}

	FILE *f = fopen(binary_path, "r+");

	if(!f) {
		printf("Couldn't open file %s\n", binary_path);
		exit(1);
	}

	bool success = true;

	fseeko(f, 0, SEEK_END);
	off_t file_size = ftello(f);
	rewind(f);

	uint32_t magic;
	fread(&magic, sizeof(uint32_t), 1, f);

	switch(magic) {
		case FAT_MAGIC:
		case FAT_CIGAM: {
			fseeko(f, 0, SEEK_SET);

			struct fat_header fh;
			fread(&fh, sizeof(fh), 1, f);

			uint32_t nfat_arch = SWAP32(fh.nfat_arch, magic);

			printf("Binary is a fat binary with %d archs.\n", nfat_arch);

			struct fat_arch archs[nfat_arch];
			fread(archs, sizeof(archs), 1, f);

			int fails = 0;

			uint32_t offset = 0;
			if(nfat_arch > 0) {
				offset = SWAP32(archs[0].offset, magic);
			}

			for(int i = 0; i < nfat_arch; i++) {
				off_t orig_offset = SWAP32(archs[i].offset, magic);
				off_t orig_slice_size = SWAP32(archs[i].size, magic);
				offset = ROUND_UP(offset, 1 << SWAP32(archs[i].align, magic));
				if(orig_offset != offset) {
					fmemmove(f, offset, orig_offset, orig_slice_size);
					fbzero(f, MIN(offset, orig_offset) + orig_slice_size, ABSDIFF(offset, orig_offset));

					archs[i].offset = SWAP32(offset, magic);
				}

				off_t slice_size = orig_slice_size;
				bool r = strip_signature(f, offset, &slice_size);
				if(!r) {
					printf("Failed to strip signature from arch #%d!\n", i + 1);
					fails++;
				}

				if(slice_size < orig_slice_size && i < nfat_arch - 1) {
					fbzero(f, offset + slice_size, orig_slice_size - slice_size);
				}

				file_size = offset + slice_size;
				offset += slice_size;
				archs[i].size = SWAP32((uint32_t)slice_size, magic);
			}

			rewind(f);
			fwrite(&fh, sizeof(fh), 1, f);
			fwrite(archs, sizeof(archs), 1, f);

			// We need to flush before truncating
			fflush(f);
			ftruncate(fileno(f), file_size);

			if(fails == 0) {
				printf("Stripped signature from  %s\n", binary_path);
			} else if(fails == nfat_arch) {
				printf("Failed to strip signature from any archs.\n");
				success = false;
			} else {
				printf("Stripped signature from %d/%d archs in %s\n", nfat_arch - fails, nfat_arch, binary_path);
			}

			break;
		}
		case MH_MAGIC_64:
		case MH_CIGAM_64:
		case MH_MAGIC:
		case MH_CIGAM:
			if(strip_signature(f, 0, &file_size)) {
				ftruncate(fileno(f), file_size);
				printf("Stripped signature from  %s\n", binary_path);
			} else {
				printf("Failed to strip signature!\n");
				success = false;
			}
			break;
		default:
			printf("Unknown magic: 0x%x\n", magic);
			exit(1);
	}

	fclose(f);

	if(!success) {
		exit(1);
	}

    return 0;
}
