/*
 *  Copyright (C) 2014 Rocco Folino <lordzen87@gmail.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <zlib.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <dirent.h>

#define MAGIC_HEADER "UBNT"
#define MAGIC_END "END."

#define MAGIC_LEN 4
#define HEADER_VERSION_MAXLEN 256
#define SECTION_NAME_MAXLEN 16
#define SECTION_PAD_LEN 12

#define FILE_SECTION_MAXLEN SECTION_NAME_MAXLEN + 5

#define FILE_PATH_MAXLEN 255

struct header {
	char version[HEADER_VERSION_MAXLEN];
	u_int32_t crc;
	u_int32_t pad;
} __attribute__ ((packed));

struct section {
	char name[SECTION_NAME_MAXLEN];
	char pad[SECTION_PAD_LEN];
	u_int32_t memaddr;
	u_int32_t index;
	u_int32_t baseaddr;
	u_int32_t entryaddr;
	u_int32_t data_size;
	u_int32_t part_size;
} __attribute__ ((packed));

struct section_crc {
	u_int32_t crc;
	u_int32_t pad;
} __attribute__ ((packed));

struct signature {
	u_int32_t crc;
	u_int32_t pad;
} __attribute__ ((packed));

#define TO_KB(_vk) (_vk / 1024)
#define TO_MB(_vm) ((float)(_vm / 1024) / 1024)

static void usage(const char *progname)
{
	printf("Usage: %s [options] <image-file>\n"
	       "\t-i\t\t\t - print image info [default option]\n"
	       "\t-x\t\t\t - extract image content\n"
	       "\t-C <location>\t\t - location\n"
	       "\t-h\t\t\t - this help\n",
	       progname);
}

static inline void printf_bin(char *buf, size_t len) {
	int i;

	for(i=0; i<len; i++) {
		if (buf[i] == '\0') {
			break;
		}
		if (isalnum(buf[i])) {
			printf("%c", buf[i]);
		} else {
			printf(".");
		}
	}
}

static void print_header_info(struct header *h)
{
	printf("Version: ");
	printf_bin(h->version, HEADER_VERSION_MAXLEN);
	printf("\n");

	printf("Header CRC: 0x%.8x\n", ntohl(h->crc));
}

static void print_section_info(struct section *s)
{
	printf("section: ");
	printf_bin(s->name, SECTION_NAME_MAXLEN);
	printf("\n");

	printf("Mem addr: 0x%.8x\n", ntohl(s->memaddr));
	printf("Index: 0x%.8x\n", ntohl(s->index));
	printf("Base addr: 0x%.8x\n", ntohl(s->baseaddr));
	printf("Entry addr: 0x%.8x\n", ntohl(s->entryaddr));
	printf("Data size: %u bytes (KB = %.1f) (MB = %.1f)\n",
	       ntohl(s->data_size),
	       (float)TO_KB(ntohl(s->data_size)),
	       (float)TO_MB(ntohl(s->data_size)));
	printf("Part size: %u bytes (KB = %.1f) (MB = %.1f)\n",
	       ntohl(s->part_size),
	       (float)TO_KB(ntohl(s->part_size)),
	       (float)TO_MB(ntohl(s->part_size)));
}

static int write_section(struct section *s, const char *data,
			 const char *location)
{
	FILE *f = NULL;
	char filename[FILE_PATH_MAXLEN];

	memset(filename, 0, FILE_PATH_MAXLEN);

	if (location)
		strncpy(filename, location, FILE_PATH_MAXLEN);

	/* XXX: manage no-name part */
	strncat(filename, s->name, FILE_PATH_MAXLEN - strlen(filename));
	strncat(filename, ".bin", FILE_PATH_MAXLEN - strlen(filename));


	printf("Extracting ");
	printf_bin(s->name, SECTION_NAME_MAXLEN);
	printf(" to %s...", filename);

	if ((f = fopen(filename, "w")) == NULL) {
		printf("\nERROR: Cannot open image file %s\n", filename);
		return EXIT_FAILURE;
	}

	if (fwrite(data, ntohl(s->data_size), 1, f) != 1) {
		printf("\nERROR: %s\n", strerror(errno));
		fclose(f);
		return -1;
	}

	printf("done\n");
	fclose(f);

	return 0;
}

int main (int argc, char **argv)
{
	int o;
	FILE *f = NULL;
	char *filename = NULL;
	const char *location = NULL;
	int extract = 0;
	char magic[MAGIC_LEN];

	while ((o = getopt(argc, argv, "hixC:")) != -1)
	{
		switch (o) {
		case 'i':
			extract = 0;
			break;
		case 'x':
			extract = 1;
			break;
		case 'C':
			location = optarg;
			break;
		case 'h':
			usage(argv[0]);
			return EXIT_SUCCESS;
		default:
			printf("ERROR: unkown argument '%c'\n\n", o);
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	if (optind >= argc) {
		printf("ERROR: no image-file\n\n");
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	filename = strdup(argv[optind]);
	if (filename == NULL) {
		printf("ERROR: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	if ((f = fopen(filename, "r")) == NULL) {
		printf("ERROR: Cannot open image file %s\n", filename);
		return EXIT_FAILURE;
	}

	if (location) {
		DIR *d = NULL;

		if (strlen(location) > (FILE_PATH_MAXLEN - FILE_SECTION_MAXLEN)) {
			printf("ERROR: location path too long\n");
			goto fail;
		}

		d = opendir(location);
		if (!d) {
			printf("ERROR: location dir: %s\n", strerror(errno));
			goto fail;
		}
		closedir(d);
	}

	printf("\nImage file: %s\n\n", filename);

	if (fread(magic, MAGIC_LEN, 1, f) != 1) {
		printf("ERROR: %s\n", strerror(errno));
		goto fail;
	}

	if (memcmp(magic, MAGIC_HEADER, 4) == 0) {
		struct header h;

		if (fread(&h, sizeof(struct header), 1, f) != 1) {
			printf("ERROR: %s\n", strerror(errno));
			goto fail;
		}

		if (!extract) {
			print_header_info(&h);
			printf("\n");
		}

	} else {
		goto fail;
	}

	while (!feof(f)) {

		if (fread(magic, MAGIC_LEN, 1, f) != 1) {
			printf("ERROR: %s\n", strerror(errno));
			goto fail;
		}

		if (memcmp(magic, MAGIC_END, 4) == 0) {

			struct signature s;

			if (fread(&s, sizeof(struct signature), 1, f) != 1) {
				printf("ERROR: %s\n", strerror(errno));
				goto fail;
			}

			if (!extract) {
				printf("Sign CRC: 0x%.8x\n", ntohl(s.crc));
				printf("\n");
			}

			break;
		} else { /* Assume a section */
			struct section s;

			if (fread(&s, sizeof(struct section), 1, f) < 1) {
				printf("ERROR: %s\n", strerror(errno));
				goto fail;
			}

			if (!extract) {
				struct section_crc scrc;

				print_section_info(&s);

				fseek(f, ntohl(s.data_size), SEEK_CUR);

				if (fread(&scrc, sizeof(struct section_crc), 1, f) != 1) {
					printf("ERROR: %s\n", strerror(errno));
					goto fail;
				}

				printf("Section CRC: 0x%.8x\n", ntohl(scrc.crc));
				printf("\n");
			} else {
				char *data = (char *)malloc(ntohl(s.data_size));
				if (data == NULL) {
					printf("ERROR: %s\n", strerror(errno));
					goto fail;
				}

				if (fread(data, ntohl(s.data_size), 1, f) != 1) {
					printf("ERROR: %s\n", strerror(errno));
					goto fail;
				}

				if (write_section(&s, (const char *)data, location) == -1) {
					goto fail;
				}

				free(data);

				fseek(f, sizeof(struct section_crc), SEEK_CUR);
			}

		}
	}

	fclose(f);
	free(filename);

	return EXIT_SUCCESS;

fail:
	fclose(f);
	free(filename);
	return EXIT_FAILURE;
}

