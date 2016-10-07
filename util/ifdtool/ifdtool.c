/*
 * ifdtool - dump Intel Firmware Descriptor information
 *
 * Copyright (C) 2011 The ChromiumOS Authors.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "ifdtool.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

static int ifd_version;
static int max_regions = 0;
static int selected_chip = 0;

static const struct region_name region_names[MAX_REGIONS] = {
	{ "Flash Descriptor", "fd" },
	{ "BIOS", "bios" },
	{ "Intel ME", "me" },
	{ "GbE", "gbe" },
	{ "Platform Data", "pd" },
	{ "Reserved", "res1" },
	{ "Reserved", "res2" },
	{ "Reserved", "res3" },
	{ "EC", "ec" },
};

static fdbar_t *find_fd(char *image, int size)
{
	int i, found = 0;

	/* Scan for FD signature */
	for (i = 0; i < (size - 4); i += 4) {
		if (*(uint32_t *) (image + i) == 0x0FF0A55A) {
			found = 1;
			break;	// signature found.
		}
	}

	if (!found) {
		printf("No Flash Descriptor found in this image\n");
		return NULL;
	}

	return (fdbar_t *) (image + i);
}

/*
 * There is no version field in the descriptor so to determine
 * if this is a new descriptor format we check the hardcoded SPI
 * read frequency to see if it is fixed at 20MHz or 17MHz.
 */
static void check_ifd_version(char *image, int size)
{
	fdbar_t *fdb = find_fd(image, size);
	fcba_t *fcba;
	int read_freq;

	if (!fdb)
		exit(EXIT_FAILURE);

	fcba = (fcba_t *) (image + (((fdb->flmap0) & 0xff) << 4));
	if (!fcba)
		exit(EXIT_FAILURE);

	read_freq = (fcba->flcomp >> 17) & 7;

	switch (read_freq) {
	case SPI_FREQUENCY_20MHZ:
		ifd_version = IFD_VERSION_1;
		max_regions = MAX_REGIONS_OLD;
		break;
	case SPI_FREQUENCY_17MHZ:
		ifd_version = IFD_VERSION_2;
		max_regions = MAX_REGIONS;
		break;
	default:
		fprintf(stderr, "Unknown descriptor version: %d\n",
			read_freq);
		exit(EXIT_FAILURE);
	}
}

static region_t get_region(frba_t *frba, int region_type)
{
	int base_mask;
	int limit_mask;
	uint32_t *flreg;
	region_t region;

	if (ifd_version >= IFD_VERSION_2)
		base_mask = 0x7fff;
	else
		base_mask = 0xfff;

	limit_mask = base_mask << 16;

	switch (region_type) {
	case 0:
		flreg = &frba->flreg0;
		break;
	case 1:
		flreg = &frba->flreg1;
		break;
	case 2:
		flreg = &frba->flreg2;
		break;
	case 3:
		flreg = &frba->flreg3;
		break;
	case 4:
		flreg = &frba->flreg4;
		break;
	case 5:
		flreg = &frba->flreg5;
		break;
	case 6:
		flreg = &frba->flreg6;
		break;
	case 7:
		flreg = &frba->flreg7;
		break;
	case 8:
		flreg = &frba->flreg8;
		break;
	default:
		fprintf(stderr, "Invalid region type %d.\n", region_type);
		exit (EXIT_FAILURE);
	}

	region.base = (*flreg & base_mask) << 12;
	region.limit = ((*flreg & limit_mask) >> 4) | 0xfff;
	region.size = region.limit - region.base + 1;

	if (region.size < 0)
		region.size = 0;

	return region;
}

static void set_region(frba_t *frba, int region_type, region_t region)
{
	switch (region_type) {
	case 0:
		frba->flreg0 = (((region.limit >> 12) & 0x7fff) << 16)
			| ((region.base >> 12) & 0x7fff);
		break;
	case 1:
		frba->flreg1 = (((region.limit >> 12) & 0x7fff) << 16)
			| ((region.base >> 12) & 0x7fff);
		break;
	case 2:
		frba->flreg2 = (((region.limit >> 12) & 0x7fff) << 16)
			| ((region.base >> 12) & 0x7fff);
		break;
	case 3:
		frba->flreg3 = (((region.limit >> 12) & 0x7fff) << 16)
			| ((region.base >> 12) & 0x7fff);
		break;
	case 4:
		frba->flreg4 = (((region.limit >> 12) & 0x7fff) << 16)
			| ((region.base >> 12) & 0x7fff);
		break;
	default:
		fprintf(stderr, "Invalid region type.\n");
		exit (EXIT_FAILURE);
	}
}

static const char *region_name(int region_type)
{
	if (region_type < 0 || region_type >= max_regions) {
		fprintf(stderr, "Invalid region type.\n");
		exit (EXIT_FAILURE);
	}

	return region_names[region_type].pretty;
}

static const char *region_name_short(int region_type)
{
	if (region_type < 0 || region_type >= max_regions) {
		fprintf(stderr, "Invalid region type.\n");
		exit (EXIT_FAILURE);
	}

	return region_names[region_type].terse;
}

static int region_num(const char *name)
{
	int i;

	for (i = 0; i < max_regions; i++) {
		if (strcasecmp(name, region_names[i].pretty) == 0)
			return i;
		if (strcasecmp(name, region_names[i].terse) == 0)
			return i;
	}

	return -1;
}

static const char *region_filename(int region_type)
{
	static const char *region_filenames[MAX_REGIONS] = {
		"flashregion_0_flashdescriptor.bin",
		"flashregion_1_bios.bin",
		"flashregion_2_intel_me.bin",
		"flashregion_3_gbe.bin",
		"flashregion_4_platform_data.bin",
		"flashregion_5_reserved.bin",
		"flashregion_6_reserved.bin",
		"flashregion_7_reserved.bin",
		"flashregion_8_ec.bin",
	};

	if (region_type < 0 || region_type >= max_regions) {
		fprintf(stderr, "Invalid region type %d.\n", region_type);
		exit (EXIT_FAILURE);
	}

	return region_filenames[region_type];
}

static void dump_region(int num, frba_t *frba)
{
	region_t region = get_region(frba, num);
	printf("  Flash Region %d (%s): %08x - %08x %s\n",
		       num, region_name(num), region.base, region.limit,
		       region.size < 1 ? "(unused)" : "");
}

static void dump_region_layout(char *buf, size_t bufsize, int num, frba_t *frba)
{
	region_t region = get_region(frba, num);
	snprintf(buf, bufsize, "%08x:%08x %s\n",
		region.base, region.limit, region_name_short(num));
}

static void dump_frba(frba_t * frba)
{
	printf("Found Region Section\n");
	printf("FLREG0:    0x%08x\n", frba->flreg0);
	dump_region(0, frba);
	printf("FLREG1:    0x%08x\n", frba->flreg1);
	dump_region(1, frba);
	printf("FLREG2:    0x%08x\n", frba->flreg2);
	dump_region(2, frba);
	printf("FLREG3:    0x%08x\n", frba->flreg3);
	dump_region(3, frba);
	printf("FLREG4:    0x%08x\n", frba->flreg4);
	dump_region(4, frba);

	if (ifd_version >= IFD_VERSION_2) {
		printf("FLREG5:    0x%08x\n", frba->flreg5);
		dump_region(5, frba);
		printf("FLREG6:    0x%08x\n", frba->flreg6);
		dump_region(6, frba);
		printf("FLREG7:    0x%08x\n", frba->flreg7);
		dump_region(7, frba);
		printf("FLREG8:    0x%08x\n", frba->flreg8);
		dump_region(8, frba);
	}
}

static void dump_frba_layout(frba_t * frba, char *layout_fname)
{
	char buf[LAYOUT_LINELEN];
	size_t bufsize = LAYOUT_LINELEN;
	int i;

	int layout_fd = open(layout_fname, O_WRONLY | O_CREAT | O_TRUNC,
			S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (layout_fd == -1) {
		perror("Could not open file");
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < max_regions; i++) {
		dump_region_layout(buf, bufsize, i, frba);
		if (write(layout_fd, buf, strlen(buf)) < 0) {
			perror("Could not write to file");
			exit(EXIT_FAILURE);
		}
	}
	close(layout_fd);
	printf("Wrote layout to %s\n", layout_fname);
}

static void decode_spi_frequency(unsigned int freq)
{
	switch (freq) {
	case SPI_FREQUENCY_20MHZ:
		printf("20MHz");
		break;
	case SPI_FREQUENCY_33MHZ:
		printf("33MHz");
		break;
	case SPI_FREQUENCY_48MHZ:
		printf("48MHz");
		break;
	case SPI_FREQUENCY_50MHZ_30MHZ:
		switch (ifd_version) {
		case IFD_VERSION_1:
			printf("50MHz");
			break;
		case IFD_VERSION_2:
			printf("30MHz");
			break;
		}
		break;
	case SPI_FREQUENCY_17MHZ:
		printf("17MHz");
		break;
	default:
		printf("unknown<%x>MHz", freq);
	}
}

static void decode_component_density(unsigned int density)
{
	switch (density) {
	case COMPONENT_DENSITY_512KB:
		printf("512KB");
		break;
	case COMPONENT_DENSITY_1MB:
		printf("1MB");
		break;
	case COMPONENT_DENSITY_2MB:
		printf("2MB");
		break;
	case COMPONENT_DENSITY_4MB:
		printf("4MB");
		break;
	case COMPONENT_DENSITY_8MB:
		printf("8MB");
		break;
	case COMPONENT_DENSITY_16MB:
		printf("16MB");
		break;
	case COMPONENT_DENSITY_32MB:
		printf("32MB");
		break;
	case COMPONENT_DENSITY_64MB:
		printf("64MB");
		break;
	case COMPONENT_DENSITY_UNUSED:
		printf("UNUSED");
		break;
	default:
		printf("unknown<%x>MB", density);
	}
}

static void dump_fcba(fcba_t * fcba)
{
	printf("\nFound Component Section\n");
	printf("FLCOMP     0x%08x\n", fcba->flcomp);
	printf("  Dual Output Fast Read Support:       %ssupported\n",
		(fcba->flcomp & (1 << 30))?"":"not ");
	printf("  Read ID/Read Status Clock Frequency: ");
	decode_spi_frequency((fcba->flcomp >> 27) & 7);
	printf("\n  Write/Erase Clock Frequency:         ");
	decode_spi_frequency((fcba->flcomp >> 24) & 7);
	printf("\n  Fast Read Clock Frequency:           ");
	decode_spi_frequency((fcba->flcomp >> 21) & 7);
	printf("\n  Fast Read Support:                   %ssupported",
		(fcba->flcomp & (1 << 20))?"":"not ");
	printf("\n  Read Clock Frequency:                ");
	decode_spi_frequency((fcba->flcomp >> 17) & 7);

	switch (ifd_version) {
	case IFD_VERSION_1:
		printf("\n  Component 2 Density:                 ");
		decode_component_density((fcba->flcomp >> 3) & 7);
		printf("\n  Component 1 Density:                 ");
		decode_component_density(fcba->flcomp & 7);
		break;
	case IFD_VERSION_2:
		printf("\n  Component 2 Density:                 ");
		decode_component_density((fcba->flcomp >> 4) & 0xf);
		printf("\n  Component 1 Density:                 ");
		decode_component_density(fcba->flcomp & 0xf);
		break;
	}

	printf("\n");
	printf("FLILL      0x%08x\n", fcba->flill);
	printf("  Invalid Instruction 3: 0x%02x\n",
		(fcba->flill >> 24) & 0xff);
	printf("  Invalid Instruction 2: 0x%02x\n",
		(fcba->flill >> 16) & 0xff);
	printf("  Invalid Instruction 1: 0x%02x\n",
		(fcba->flill >> 8) & 0xff);
	printf("  Invalid Instruction 0: 0x%02x\n",
		fcba->flill & 0xff);
	printf("FLPB       0x%08x\n", fcba->flpb);
	printf("  Flash Partition Boundary Address: 0x%06x\n\n",
		(fcba->flpb & 0xfff) << 12);
}

static void dump_fpsba(fpsba_t * fpsba)
{
	printf("Found PCH Strap Section\n");
	printf("PCHSTRP0:  0x%08x\n", fpsba->pchstrp0);
	printf("PCHSTRP1:  0x%08x\n", fpsba->pchstrp1);
	printf("PCHSTRP2:  0x%08x\n", fpsba->pchstrp2);
	printf("PCHSTRP3:  0x%08x\n", fpsba->pchstrp3);
	printf("PCHSTRP4:  0x%08x\n", fpsba->pchstrp4);
	printf("PCHSTRP5:  0x%08x\n", fpsba->pchstrp5);
	printf("PCHSTRP6:  0x%08x\n", fpsba->pchstrp6);
	printf("PCHSTRP7:  0x%08x\n", fpsba->pchstrp7);
	printf("PCHSTRP8:  0x%08x\n", fpsba->pchstrp8);
	printf("PCHSTRP9:  0x%08x\n", fpsba->pchstrp9);
	printf("PCHSTRP10: 0x%08x\n", fpsba->pchstrp10);
	printf("PCHSTRP11: 0x%08x\n", fpsba->pchstrp11);
	printf("PCHSTRP12: 0x%08x\n", fpsba->pchstrp12);
	printf("PCHSTRP13: 0x%08x\n", fpsba->pchstrp13);
	printf("PCHSTRP14: 0x%08x\n", fpsba->pchstrp14);
	printf("PCHSTRP15: 0x%08x\n", fpsba->pchstrp15);
	printf("PCHSTRP16: 0x%08x\n", fpsba->pchstrp16);
	printf("PCHSTRP17: 0x%08x\n\n", fpsba->pchstrp17);
}

static void decode_flmstr(uint32_t flmstr)
{
	int wr_shift, rd_shift;
	if (ifd_version >= IFD_VERSION_2) {
		wr_shift = FLMSTR_WR_SHIFT_V2;
		rd_shift = FLMSTR_RD_SHIFT_V2;
	} else {
		wr_shift = FLMSTR_WR_SHIFT_V1;
		rd_shift = FLMSTR_RD_SHIFT_V1;
	}

	/* EC region access only available on v2+ */
	if (ifd_version >= IFD_VERSION_2)
		printf("  EC Region Write Access:            %s\n",
		       (flmstr & (1 << (wr_shift + 8))) ?
		       "enabled" : "disabled");
	printf("  Platform Data Region Write Access: %s\n",
		(flmstr & (1 << (wr_shift + 4))) ? "enabled" : "disabled");
	printf("  GbE Region Write Access:           %s\n",
		(flmstr & (1 << (wr_shift + 3))) ? "enabled" : "disabled");
	printf("  Intel ME Region Write Access:      %s\n",
		(flmstr & (1 << (wr_shift + 2))) ? "enabled" : "disabled");
	printf("  Host CPU/BIOS Region Write Access: %s\n",
		(flmstr & (1 << (wr_shift + 1))) ? "enabled" : "disabled");
	printf("  Flash Descriptor Write Access:     %s\n",
		(flmstr & (1 << wr_shift)) ? "enabled" : "disabled");

	if (ifd_version >= IFD_VERSION_2)
		printf("  EC Region Read Access:             %s\n",
		       (flmstr & (1 << (rd_shift + 8))) ?
		       "enabled" : "disabled");
	printf("  Platform Data Region Read Access:  %s\n",
		(flmstr & (1 << (rd_shift + 4))) ? "enabled" : "disabled");
	printf("  GbE Region Read Access:            %s\n",
		(flmstr & (1 << (rd_shift + 3))) ? "enabled" : "disabled");
	printf("  Intel ME Region Read Access:       %s\n",
		(flmstr & (1 << (rd_shift + 2))) ? "enabled" : "disabled");
	printf("  Host CPU/BIOS Region Read Access:  %s\n",
		(flmstr & (1 << (rd_shift + 1))) ? "enabled" : "disabled");
	printf("  Flash Descriptor Read Access:      %s\n",
		(flmstr & (1 << rd_shift)) ? "enabled" : "disabled");

	/* Requestor ID doesn't exist for ifd 2 */
	if (ifd_version < IFD_VERSION_2)
		printf("  Requester ID:                      0x%04x\n\n",
			flmstr & 0xffff);
}

static void dump_fmba(fmba_t * fmba)
{
	printf("Found Master Section\n");
	printf("FLMSTR1:   0x%08x (Host CPU/BIOS)\n", fmba->flmstr1);
	decode_flmstr(fmba->flmstr1);
	printf("FLMSTR2:   0x%08x (Intel ME)\n", fmba->flmstr2);
	decode_flmstr(fmba->flmstr2);
	printf("FLMSTR3:   0x%08x (GbE)\n", fmba->flmstr3);
	decode_flmstr(fmba->flmstr3);
	if (ifd_version >= IFD_VERSION_2) {
		printf("FLMSTR5:   0x%08x (EC)\n", fmba->flmstr5);
		decode_flmstr(fmba->flmstr5);
	}
}

static void dump_fmsba(fmsba_t * fmsba)
{
	printf("Found Processor Strap Section\n");
	printf("????:      0x%08x\n", fmsba->data[0]);
	printf("????:      0x%08x\n", fmsba->data[1]);
	printf("????:      0x%08x\n", fmsba->data[2]);
	printf("????:      0x%08x\n", fmsba->data[3]);
}

static void dump_jid(uint32_t jid)
{
	printf("    SPI Componend Device ID 1:          0x%02x\n",
		(jid >> 16) & 0xff);
	printf("    SPI Componend Device ID 0:          0x%02x\n",
		(jid >> 8) & 0xff);
	printf("    SPI Componend Vendor ID:            0x%02x\n",
		jid & 0xff);
}

static void dump_vscc(uint32_t vscc)
{
	printf("    Lower Erase Opcode:                 0x%02x\n",
		vscc >> 24);
	printf("    Lower Write Enable on Write Status: 0x%02x\n",
		vscc & (1 << 20) ? 0x06 : 0x50);
	printf("    Lower Write Status Required:        %s\n",
		vscc & (1 << 19) ? "Yes" : "No");
	printf("    Lower Write Granularity:            %d bytes\n",
		vscc & (1 << 18) ? 64 : 1);
	printf("    Lower Block / Sector Erase Size:    ");
	switch ((vscc >> 16) & 0x3) {
	case 0:
		printf("256 Byte\n");
		break;
	case 1:
		printf("4KB\n");
		break;
	case 2:
		printf("8KB\n");
		break;
	case 3:
		printf("64KB\n");
		break;
	}

	printf("    Upper Erase Opcode:                 0x%02x\n",
		(vscc >> 8) & 0xff);
	printf("    Upper Write Enable on Write Status: 0x%02x\n",
		vscc & (1 << 4) ? 0x06 : 0x50);
	printf("    Upper Write Status Required:        %s\n",
		vscc & (1 << 3) ? "Yes" : "No");
	printf("    Upper Write Granularity:            %d bytes\n",
		vscc & (1 << 2) ? 64 : 1);
	printf("    Upper Block / Sector Erase Size:    ");
	switch (vscc & 0x3) {
	case 0:
		printf("256 Byte\n");
		break;
	case 1:
		printf("4KB\n");
		break;
	case 2:
		printf("8KB\n");
		break;
	case 3:
		printf("64KB\n");
		break;
	}
}

static void dump_vtba(vtba_t *vtba, int vtl)
{
	int i;
	int num = (vtl >> 1) < 8 ? (vtl >> 1) : 8;

	printf("ME VSCC table:\n");
	for (i = 0; i < num; i++) {
		printf("  JID%d:  0x%08x\n", i, vtba->entry[i].jid);
		dump_jid(vtba->entry[i].jid);
		printf("  VSCC%d: 0x%08x\n", i, vtba->entry[i].vscc);
		dump_vscc(vtba->entry[i].vscc);
	}
	printf("\n");
}

static void dump_oem(uint8_t *oem)
{
	int i, j;
	printf("OEM Section:\n");
	for (i = 0; i < 4; i++) {
		printf("%02x:", i << 4);
		for (j = 0; j < 16; j++)
			printf(" %02x", oem[(i<<4)+j]);
		printf ("\n");
	}
	printf ("\n");
}

static void dump_fd(char *image, int size)
{
	fdbar_t *fdb = find_fd(image, size);
	if (!fdb)
		exit(EXIT_FAILURE);

	printf("FLMAP0:    0x%08x\n", fdb->flmap0);
	printf("  NR:      %d\n", (fdb->flmap0 >> 24) & 7);
	printf("  FRBA:    0x%x\n", ((fdb->flmap0 >> 16) & 0xff) << 4);
	printf("  NC:      %d\n", ((fdb->flmap0 >> 8) & 3) + 1);
	printf("  FCBA:    0x%x\n", ((fdb->flmap0) & 0xff) << 4);

	printf("FLMAP1:    0x%08x\n", fdb->flmap1);
	printf("  ISL:     0x%02x\n", (fdb->flmap1 >> 24) & 0xff);
	printf("  FPSBA:   0x%x\n", ((fdb->flmap1 >> 16) & 0xff) << 4);
	printf("  NM:      %d\n", (fdb->flmap1 >> 8) & 3);
	printf("  FMBA:    0x%x\n", ((fdb->flmap1) & 0xff) << 4);

	printf("FLMAP2:    0x%08x\n", fdb->flmap2);
	printf("  PSL:     0x%04x\n", (fdb->flmap2 >> 8) & 0xffff);
	printf("  FMSBA:   0x%x\n", ((fdb->flmap2) & 0xff) << 4);

	printf("FLUMAP1:   0x%08x\n", fdb->flumap1);
	printf("  Intel ME VSCC Table Length (VTL):        %d\n",
		(fdb->flumap1 >> 8) & 0xff);
	printf("  Intel ME VSCC Table Base Address (VTBA): 0x%06x\n\n",
		(fdb->flumap1 & 0xff) << 4);
	dump_vtba((vtba_t *)
			(image + ((fdb->flumap1 & 0xff) << 4)),
			(fdb->flumap1 >> 8) & 0xff);
	dump_oem((uint8_t *)image + 0xf00);
	dump_frba((frba_t *)
			(image + (((fdb->flmap0 >> 16) & 0xff) << 4)));
	dump_fcba((fcba_t *) (image + (((fdb->flmap0) & 0xff) << 4)));
	dump_fpsba((fpsba_t *)
			(image + (((fdb->flmap1 >> 16) & 0xff) << 4)));
	dump_fmba((fmba_t *) (image + (((fdb->flmap1) & 0xff) << 4)));
	dump_fmsba((fmsba_t *) (image + (((fdb->flmap2) & 0xff) << 4)));
}

static void dump_layout(char *image, int size, char *layout_fname)
{
	fdbar_t *fdb = find_fd(image, size);
	if (!fdb)
		exit(EXIT_FAILURE);

	dump_frba_layout((frba_t *)
			(image + (((fdb->flmap0 >> 16) & 0xff) << 4)),
			layout_fname);
}

static void write_regions(char *image, int size)
{
	int i;

	fdbar_t *fdb = find_fd(image, size);
	if (!fdb)
		exit(EXIT_FAILURE);

	frba_t *frba =
	    (frba_t *) (image + (((fdb->flmap0 >> 16) & 0xff) << 4));

	for (i = 0; i < max_regions; i++) {
		region_t region = get_region(frba, i);
		dump_region(i, frba);
		if (region.size > 0) {
			int region_fd;
			region_fd = open(region_filename(i),
					 O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
					 S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
			if (region_fd < 0) {
				perror("Error while trying to open file");
				exit(EXIT_FAILURE);
			}
			if (write(region_fd, image + region.base, region.size) != region.size)
				perror("Error while writing");
			close(region_fd);
		}
	}
}

static void write_image(char *filename, char *image, int size)
{
	char new_filename[FILENAME_MAX]; // allow long file names
	int new_fd;

	// - 5: leave room for ".new\0"
	strncpy(new_filename, filename, FILENAME_MAX - 5);
	strncat(new_filename, ".new", FILENAME_MAX - strlen(filename));

	printf("Writing new image to %s\n", new_filename);

	// Now write out new image
	new_fd = open(new_filename,
			 O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
			 S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (new_fd < 0) {
		perror("Error while trying to open file");
		exit(EXIT_FAILURE);
	}
	if (write(new_fd, image, size) != size)
		perror("Error while writing");
	close(new_fd);
}

static void set_spi_frequency(char *filename, char *image, int size,
			      enum spi_frequency freq)
{
	fdbar_t *fdb = find_fd(image, size);
	fcba_t *fcba = (fcba_t *) (image + (((fdb->flmap0) & 0xff) << 4));

	/* clear bits 21-29 */
	fcba->flcomp &= ~0x3fe00000;
	/* Read ID and Read Status Clock Frequency */
	fcba->flcomp |= freq << 27;
	/* Write and Erase Clock Frequency */
	fcba->flcomp |= freq << 24;
	/* Fast Read Clock Frequency */
	fcba->flcomp |= freq << 21;

	write_image(filename, image, size);
}

static void set_em100_mode(char *filename, char *image, int size)
{
	fdbar_t *fdb = find_fd(image, size);
	fcba_t *fcba = (fcba_t *) (image + (((fdb->flmap0) & 0xff) << 4));
	int freq;

	switch (ifd_version) {
	case IFD_VERSION_1:
		freq = SPI_FREQUENCY_20MHZ;
		break;
	case IFD_VERSION_2:
		freq = SPI_FREQUENCY_17MHZ;
		break;
	default:
		freq = SPI_FREQUENCY_17MHZ;
		break;
	}

	fcba->flcomp &= ~(1 << 30);
	set_spi_frequency(filename, image, size, freq);
}

static void set_chipdensity(char *filename, char *image, int size,
                            unsigned int density)
{
	fdbar_t *fdb = find_fd(image, size);
	fcba_t *fcba = (fcba_t *) (image + (((fdb->flmap0) & 0xff) << 4));

	printf("Setting chip density to ");
	decode_component_density(density);
	printf("\n");

	switch (ifd_version) {
	case IFD_VERSION_1:
		/* fail if selected density is not supported by this version */
		if ( (density == COMPONENT_DENSITY_32MB) ||
		     (density == COMPONENT_DENSITY_64MB) ||
		     (density == COMPONENT_DENSITY_UNUSED) ) {
			printf("error: Selected density not supported in IFD version 1.\n");
			exit(EXIT_FAILURE);
		}
		break;
	case IFD_VERSION_2:
		/* I do not have a version 2 IFD nor do i have the docs. */
		printf("error: Changing the chip density for IFD version 2 has not been"
		       " implemented yet.\n");
		exit(EXIT_FAILURE);
	default:
		printf("error: Unknown IFD version\n");
		exit(EXIT_FAILURE);
		break;
	}

	/* clear chip density for corresponding chip */
	switch (selected_chip) {
	case 1:
		fcba->flcomp &= ~(0x7);
		break;
	case 2:
		fcba->flcomp &= ~(0x7 << 3);
		break;
	default: /*both chips*/
		fcba->flcomp &= ~(0x3F);
		break;
	}

	/* set the new density */
	if (selected_chip == 1 || selected_chip == 0)
		fcba->flcomp |= (density); /* first chip */
	if (selected_chip == 2 || selected_chip == 0)
		fcba->flcomp |= (density << 3); /* second chip */

	write_image(filename, image, size);
}

static void lock_descriptor(char *filename, char *image, int size)
{
	int wr_shift, rd_shift;
	fdbar_t *fdb = find_fd(image, size);
	fmba_t *fmba = (fmba_t *) (image + (((fdb->flmap1) & 0xff) << 4));
	/* TODO: Dynamically take Platform Data Region and GbE Region
	 * into regard.
	 */

	if (ifd_version >= IFD_VERSION_2) {
		wr_shift = FLMSTR_WR_SHIFT_V2;
		rd_shift = FLMSTR_RD_SHIFT_V2;

		/* Clear non-reserved bits */
		fmba->flmstr1 &= 0xff;
		fmba->flmstr2 &= 0xff;
		fmba->flmstr3 &= 0xff;
	} else {
		wr_shift = FLMSTR_WR_SHIFT_V1;
		rd_shift = FLMSTR_RD_SHIFT_V1;

		fmba->flmstr1 = 0;
		fmba->flmstr2 = 0;
		/* Requestor ID */
		fmba->flmstr3 = 0x118;
	}

	/* CPU/BIOS can read descriptor, BIOS, and GbE. */
	fmba->flmstr1 |= 0xb << rd_shift;
	/* CPU/BIOS can write BIOS and GbE. */
	fmba->flmstr1 |= 0xa << wr_shift;
	/* ME can read descriptor, ME, and GbE. */
	fmba->flmstr2 |= 0xd << rd_shift;
	/* ME can write ME and GbE. */
	fmba->flmstr2 |= 0xc << wr_shift;
	/* GbE can write only GbE. */
	fmba->flmstr3 |= 0x8 << rd_shift;
	/* GbE can read only GbE. */
	fmba->flmstr3 |= 0x8 << wr_shift;

	write_image(filename, image, size);
}

static void unlock_descriptor(char *filename, char *image, int size)
{
	fdbar_t *fdb = find_fd(image, size);
	fmba_t *fmba = (fmba_t *) (image + (((fdb->flmap1) & 0xff) << 4));

	if (ifd_version >= IFD_VERSION_2) {
		/* Access bits for each region are read: 19:8 write: 31:20 */
		fmba->flmstr1 = 0xffffff00 | (fmba->flmstr1 & 0xff);
		fmba->flmstr2 = 0xffffff00 | (fmba->flmstr2 & 0xff);
		fmba->flmstr3 = 0xffffff00 | (fmba->flmstr3 & 0xff);
	} else {
		fmba->flmstr1 = 0xffff0000;
		fmba->flmstr2 = 0xffff0000;
		fmba->flmstr3 = 0x08080118;
	}

	write_image(filename, image, size);
}

void inject_region(char *filename, char *image, int size, int region_type,
		   char *region_fname)
{
	fdbar_t *fdb = find_fd(image, size);
	if (!fdb)
		exit(EXIT_FAILURE);
	frba_t *frba =
	    (frba_t *) (image + (((fdb->flmap0 >> 16) & 0xff) << 4));

	region_t region = get_region(frba, region_type);
	if (region.size <= 0xfff) {
		fprintf(stderr, "Region %s is disabled in target. Not injecting.\n",
				region_name(region_type));
		exit(EXIT_FAILURE);
	}

	int region_fd = open(region_fname, O_RDONLY | O_BINARY);
	if (region_fd == -1) {
		perror("Could not open file");
		exit(EXIT_FAILURE);
	}
	struct stat buf;
	if (fstat(region_fd, &buf) == -1) {
		perror("Could not stat file");
		exit(EXIT_FAILURE);
	}
	int region_size = buf.st_size;

	printf("File %s is %d bytes\n", region_fname, region_size);

	if ( (region_size > region.size) || ((region_type != 1) &&
		(region_size > region.size))) {
		fprintf(stderr, "Region %s is %d(0x%x) bytes. File is %d(0x%x)"
				" bytes. Not injecting.\n",
				region_name(region_type), region.size,
				region.size, region_size, region_size);
		exit(EXIT_FAILURE);
	}

	int offset = 0;
	if ((region_type == 1) && (region_size < region.size)) {
		fprintf(stderr, "Region %s is %d(0x%x) bytes. File is %d(0x%x)"
				" bytes. Padding before injecting.\n",
				region_name(region_type), region.size,
				region.size, region_size, region_size);
		offset = region.size - region_size;
		memset(image + region.base, 0xff, offset);
	}

	if (size < region.base + offset + region_size) {
		fprintf(stderr, "Output file is too small. (%d < %d)\n",
			size, region.base + offset + region_size);
		exit(EXIT_FAILURE);
	}

	if (read(region_fd, image + region.base + offset, region_size)
							!= region_size) {
		perror("Could not read file");
		exit(EXIT_FAILURE);
	}

	close(region_fd);

	printf("Adding %s as the %s section of %s\n",
	       region_fname, region_name(region_type), filename);
	write_image(filename, image, size);
}

unsigned int next_pow2(unsigned int x)
{
	unsigned int y = 1;
	if (x == 0)
		return 0;
	while (y <= x)
		y = y << 1;

	return y;
}

/**
 * Determine if two memory regions overlap.
 *
 * @param r1, r2 Memory regions to compare.
 * @return 0 if the two regions are seperate
 * @return 1 if the two regions overlap
 */
static int regions_collide(region_t r1, region_t r2)
{
	if ((r1.size == 0) || (r2.size == 0))
		return 0;

	if ( ((r1.base >= r2.base) && (r1.base <= r2.limit)) ||
	     ((r1.limit >= r2.base) && (r1.limit <= r2.limit)) )
		return 1;

	return 0;
}

void new_layout(char *filename, char *image, int size, char *layout_fname)
{
	FILE *romlayout;
	char tempstr[256];
	char layout_region_name[256];
	int i, j;
	int region_number;
	region_t current_regions[MAX_REGIONS];
	region_t new_regions[MAX_REGIONS];
	int new_extent = 0;
	char *new_image;

	/* load current descriptor map and regions */
	fdbar_t *fdb = find_fd(image, size);
	if (!fdb)
		exit(EXIT_FAILURE);

	frba_t *frba =
	    (frba_t *) (image + (((fdb->flmap0 >> 16) & 0xff) << 4));

	for (i = 0; i < max_regions; i++) {
		current_regions[i] = get_region(frba, i);
		new_regions[i] = get_region(frba, i);
	}

	/* read new layout */
	romlayout = fopen(layout_fname, "r");

	if (!romlayout) {
		perror("Could not read layout file.\n");
		exit(EXIT_FAILURE);
	}

	while (!feof(romlayout)) {
		char *tstr1, *tstr2;

		if (2 != fscanf(romlayout, "%255s %255s\n", tempstr,
					layout_region_name))
			continue;

		region_number = region_num(layout_region_name);
		if (region_number < 0)
			continue;

		tstr1 = strtok(tempstr, ":");
		tstr2 = strtok(NULL, ":");
		if (!tstr1 || !tstr2) {
			fprintf(stderr, "Could not parse layout file.\n");
			exit(EXIT_FAILURE);
		}
		new_regions[region_number].base = strtol(tstr1,
				(char **)NULL, 16);
		new_regions[region_number].limit = strtol(tstr2,
				(char **)NULL, 16);
		new_regions[region_number].size =
			new_regions[region_number].limit -
			new_regions[region_number].base + 1;

		if (new_regions[region_number].size < 0)
			new_regions[region_number].size = 0;
	}
	fclose(romlayout);

	/* check new layout */
	for (i = 0; i < max_regions; i++) {
		if (new_regions[i].size == 0)
			continue;

		if (new_regions[i].size < current_regions[i].size) {
			printf("DANGER: Region %s is shrinking.\n",
					region_name(i));
			printf("    The region will be truncated to fit.\n");
			printf("    This may result in an unusable image.\n");
		}

		for (j = i + 1; j < max_regions; j++) {
			if (regions_collide(new_regions[i], new_regions[j])) {
				fprintf(stderr, "Regions would overlap.\n");
				exit(EXIT_FAILURE);
			}
		}

		/* detect if the image size should grow */
		if (new_extent < new_regions[i].limit)
			new_extent = new_regions[i].limit;
	}

	new_extent = next_pow2(new_extent - 1);
	if (new_extent != size) {
		printf("The image has changed in size.\n");
		printf("The old image is %d bytes.\n", size);
		printf("The new image is %d bytes.\n", new_extent);
	}

	/* copy regions to a new image */
	new_image = malloc(new_extent);
	memset(new_image, 0xff, new_extent);
	for (i = 0; i < max_regions; i++) {
		int copy_size = new_regions[i].size;
		int offset_current = 0, offset_new = 0;
		region_t current = current_regions[i];
		region_t new = new_regions[i];

		if (new.size == 0)
			continue;

		if (new.size > current.size) {
			/* copy from the end of the current region */
			copy_size = current.size;
			offset_new = new.size - current.size;
		}

		if (new.size < current.size) {
			/* copy to the end of the new region */
			offset_current = current.size - new.size;
		}

		printf("Copy Descriptor %d (%s) (%d bytes)\n", i,
				region_name(i), copy_size);
		printf("   from %08x+%08x:%08x (%10d)\n", current.base,
				offset_current, current.limit, current.size);
		printf("     to %08x+%08x:%08x (%10d)\n", new.base,
				offset_new, new.limit, new.size);

		memcpy(new_image + new.base + offset_new,
				image + current.base + offset_current,
				copy_size);
	}

	/* update new descriptor regions */
	fdb = find_fd(new_image, new_extent);
	if (!fdb)
		exit(EXIT_FAILURE);

	frba = (frba_t *) (new_image + (((fdb->flmap0 >> 16) & 0xff) << 4));
	for (i = 1; i < max_regions; i++) {
		set_region(frba, i, new_regions[i]);
	}

	write_image(filename, new_image, new_extent);
	free(new_image);
}

static void print_version(void)
{
	printf("ifdtool v%s -- ", IFDTOOL_VERSION);
	printf("Copyright (C) 2011 Google Inc.\n\n");
	printf
	    ("This program is free software: you can redistribute it and/or modify\n"
	     "it under the terms of the GNU General Public License as published by\n"
	     "the Free Software Foundation, version 2 of the License.\n\n"
	     "This program is distributed in the hope that it will be useful,\n"
	     "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	     "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
	     "GNU General Public License for more details.\n\n");
}

static void print_usage(const char *name)
{
	printf("usage: %s [-vhdix?] <filename>\n", name);
	printf("\n"
	       "   -d | --dump:                       dump intel firmware descriptor\n"
	       "   -f | --layout <filename>           dump regions into a flashrom layout file\n"
	       "   -x | --extract:                    extract intel fd modules\n"
	       "   -i | --inject <region>:<module>    inject file <module> into region <region>\n"
	       "   -n | --newlayout <filename>        update regions using a flashrom layout file\n"
	       "   -s | --spifreq <17|20|30|33|48|50> set the SPI frequency\n"
	       "   -D | --density <512|1|2|4|8|16>    set chip density (512 in KByte, others in MByte)\n"
	       "   -C | --chip <0|1|2>                select spi chip on which to operate\n"
	       "                                      can only be used once per run:\n"
	       "                                      0 - both chips (default), 1 - first chip, 2 - second chip\n"
	       "   -e | --em100                       set SPI frequency to 20MHz and disable\n"
	       "                                      Dual Output Fast Read Support\n"
	       "   -l | --lock                        Lock firmware descriptor and ME region\n"
	       "   -u | --unlock                      Unlock firmware descriptor and ME region\n"
	       "   -v | --version:                    print the version\n"
	       "   -h | --help:                       print this help\n\n"
	       "<region> is one of Descriptor, BIOS, ME, GbE, Platform\n"
	       "\n");
}

int main(int argc, char *argv[])
{
	int opt, option_index = 0;
	int mode_dump = 0, mode_extract = 0, mode_inject = 0, mode_spifreq = 0;
	int mode_em100 = 0, mode_locked = 0, mode_unlocked = 0;
	int mode_layout = 0, mode_newlayout = 0, mode_density = 0;
	char *region_type_string = NULL, *region_fname = NULL, *layout_fname = NULL;
	int region_type = -1, inputfreq = 0;
	unsigned int new_density = 0;
	enum spi_frequency spifreq = SPI_FREQUENCY_20MHZ;

	static struct option long_options[] = {
		{"dump", 0, NULL, 'd'},
		{"layout", 1, NULL, 'f'},
		{"extract", 0, NULL, 'x'},
		{"inject", 1, NULL, 'i'},
		{"newlayout", 1, NULL, 'n'},
		{"spifreq", 1, NULL, 's'},
		{"density", 1, NULL, 'D'},
		{"chip", 1, NULL, 'C'},
		{"em100", 0, NULL, 'e'},
		{"lock", 0, NULL, 'l'},
		{"unlock", 0, NULL, 'u'},
		{"version", 0, NULL, 'v'},
		{"help", 0, NULL, 'h'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long(argc, argv, "df:D:C:xi:n:s:eluvh?",
				  long_options, &option_index)) != EOF) {
		switch (opt) {
		case 'd':
			mode_dump = 1;
			break;
		case 'f':
			mode_layout = 1;
			layout_fname = strdup(optarg);
			if (!layout_fname) {
				fprintf(stderr, "No layout file specified\n");
				print_usage(argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		case 'x':
			mode_extract = 1;
			break;
		case 'i':
			// separate type and file name
			region_type_string = strdup(optarg);
			region_fname = strchr(region_type_string, ':');
			if (!region_fname) {
				print_usage(argv[0]);
				exit(EXIT_FAILURE);
			}
			region_fname[0] = '\0';
			region_fname++;
			// Descriptor, BIOS, ME, GbE, Platform
			// valid type?
			if (!strcasecmp("Descriptor", region_type_string))
				region_type = 0;
			else if (!strcasecmp("BIOS", region_type_string))
				region_type = 1;
			else if (!strcasecmp("ME", region_type_string))
				region_type = 2;
			else if (!strcasecmp("GbE", region_type_string))
				region_type = 3;
			else if (!strcasecmp("Platform", region_type_string))
				region_type = 4;
			else if (!strcasecmp("EC", region_type_string))
				region_type = 8;
			if (region_type == -1) {
				fprintf(stderr, "No such region type: '%s'\n\n",
					region_type_string);
				print_usage(argv[0]);
				exit(EXIT_FAILURE);
			}
			mode_inject = 1;
			break;
		case 'n':
			mode_newlayout = 1;
			layout_fname = strdup(optarg);
			if (!layout_fname) {
				fprintf(stderr, "No layout file specified\n");
				print_usage(argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		case 'D':
			mode_density = 1;
			new_density = strtoul(optarg, NULL, 0);
			switch (new_density) {
			case 512:
				new_density = COMPONENT_DENSITY_512KB;
				break;
			case 1:
				new_density = COMPONENT_DENSITY_1MB;
				break;
			case 2:
				new_density = COMPONENT_DENSITY_2MB;
				break;
			case 4:
				new_density = COMPONENT_DENSITY_4MB;
				break;
			case 8:
				new_density = COMPONENT_DENSITY_8MB;
				break;
			case 16:
				new_density = COMPONENT_DENSITY_16MB;
				break;
			case 32:
				new_density = COMPONENT_DENSITY_32MB;
				break;
			case 64:
				new_density = COMPONENT_DENSITY_64MB;
				break;
			case 0:
				new_density = COMPONENT_DENSITY_UNUSED;
				break;
			default:
				printf("error: Unknown density\n");
				print_usage(argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		case 'C':
			selected_chip = strtol(optarg, NULL, 0);
			if (selected_chip > 2) {
				fprintf(stderr, "error: Invalid chip selection\n");
				print_usage(argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		case 's':
			// Parse the requested SPI frequency
			inputfreq = strtol(optarg, NULL, 0);
			switch (inputfreq) {
			case 17:
				spifreq = SPI_FREQUENCY_17MHZ;
				break;
			case 20:
				spifreq = SPI_FREQUENCY_20MHZ;
				break;
			case 30:
				spifreq = SPI_FREQUENCY_50MHZ_30MHZ;
				break;
			case 33:
				spifreq = SPI_FREQUENCY_33MHZ;
				break;
			case 48:
				spifreq = SPI_FREQUENCY_48MHZ;
				break;
			case 50:
				spifreq = SPI_FREQUENCY_50MHZ_30MHZ;
				break;
			default:
				fprintf(stderr, "Invalid SPI Frequency: %d\n",
					inputfreq);
				print_usage(argv[0]);
				exit(EXIT_FAILURE);
			}
			mode_spifreq = 1;
			break;
		case 'e':
			mode_em100 = 1;
			break;
		case 'l':
			mode_locked = 1;
			if (mode_unlocked == 1) {
				fprintf(stderr, "Locking/Unlocking FD and ME are mutually exclusive\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'u':
			mode_unlocked = 1;
			if (mode_locked == 1) {
				fprintf(stderr, "Locking/Unlocking FD and ME are mutually exclusive\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'v':
			print_version();
			exit(EXIT_SUCCESS);
			break;
		case 'h':
		case '?':
		default:
			print_usage(argv[0]);
			exit(EXIT_SUCCESS);
			break;
		}
	}

	if ((mode_dump + mode_layout + mode_extract + mode_inject +
		mode_newlayout + (mode_spifreq | mode_em100 | mode_unlocked |
		 mode_locked)) > 1) {
		fprintf(stderr, "You may not specify more than one mode.\n\n");
		print_usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	if ((mode_dump + mode_layout + mode_extract + mode_inject +
	     mode_newlayout + mode_spifreq + mode_em100 + mode_locked +
	     mode_unlocked + mode_density) == 0) {
		fprintf(stderr, "You need to specify a mode.\n\n");
		print_usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	if (optind + 1 != argc) {
		fprintf(stderr, "You need to specify a file.\n\n");
		print_usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	char *filename = argv[optind];
	int bios_fd = open(filename, O_RDONLY | O_BINARY);
	if (bios_fd == -1) {
		perror("Could not open file");
		exit(EXIT_FAILURE);
	}
	struct stat buf;
	if (fstat(bios_fd, &buf) == -1) {
		perror("Could not stat file");
		exit(EXIT_FAILURE);
	}
	int size = buf.st_size;

	printf("File %s is %d bytes\n", filename, size);

	char *image = malloc(size);
	if (!image) {
		printf("Out of memory.\n");
		exit(EXIT_FAILURE);
	}

	if (read(bios_fd, image, size) != size) {
		perror("Could not read file");
		exit(EXIT_FAILURE);
	}

	close(bios_fd);

	check_ifd_version(image, size);

	if (mode_dump)
		dump_fd(image, size);

	if (mode_layout)
		dump_layout(image, size, layout_fname);

	if (mode_extract)
		write_regions(image, size);

	if (mode_inject)
		inject_region(filename, image, size, region_type,
				region_fname);

	if (mode_newlayout)
		new_layout(filename, image, size, layout_fname);

	if (mode_spifreq)
		set_spi_frequency(filename, image, size, spifreq);

	if (mode_density)
		set_chipdensity(filename, image, size, new_density);

	if (mode_em100)
		set_em100_mode(filename, image, size);

	if (mode_locked)
		lock_descriptor(filename, image, size);

	if (mode_unlocked)
		unlock_descriptor(filename, image, size);

	free(image);

	return 0;
}
