#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#undef _GNU_SOURCE
#define _GNU_SOURCE
#include <string.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>

#include "defs.h"
#include "loader.h"
#include "regs.h"
#include "mem.h"
#include "hw.h"
#include "rtc.h"
#include "rc.h"
#include "lcd.h"
#include "inflate.h"
#include "miniz.h"
#define XZ_USE_CRC64
#include "xz/xz.h"
#include "save.h"
#include "sound.h"
#include "sys.h"

#ifdef _MSC_VER
#define strdup _strdup
#endif

#if defined(_MSC_VER) && __STDC_WANT_SECURE_LIB__
#define strncpy(a,b,c) strncpy_s(a,c,b,c)
#define strncat(a,b,c) strncat_s(a,c,b,c)
#endif

static int mbc_table[256] =
{
	0, 1, 1, 1, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 3,
	3, 3, 3, 3, 0, 0, 0, 0, 0, 5, 5, 5, MBC_RUMBLE, MBC_RUMBLE, MBC_RUMBLE, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, MBC_HUC3, MBC_HUC1
};

static int rtc_table[256] =
{
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0
};

static int batt_table[256] =
{
	0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0,
	1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0,
	0
};

static int romsize_table[256] =
{
	2, 4, 8, 16, 32, 64, 128, 256, 512,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 128, 128, 128
	/* 0, 0, 72, 80, 96  -- actual values but bad to use these! */
};

static int ramsize_table[256] =
{
	1, 1, 1, 4, 16,
	4 /* FIXME - what value should this be?! */
};


static char *bootroms[2];
static char *romfile;
static char *sramfile;
static char *rtcfile;
static char *saveprefix;

static char *savename;
static char *savedir;

static int saveslot;

static int forcebatt, nobatt;
static int forcedmg, gbamode;

static int memfill = -1, memrand = -1;


static void initmem(void *mem, int size)
{
	char *p = mem;
	if (memrand >= 0)
	{
		srand(memrand ? memrand : (unsigned int)time(0));
		while(size--) *(p++) = rand();
	}
	else if (memfill >= 0)
		memset(p, memfill, size);
}

static byte *loadfile(FILE *f, int *len)
{
	int c, l = 0, p = 0;
	byte *d = NULL, buf[4096];

	for(;;)
	{
		c = (int)fread(buf, 1, sizeof buf, f);
		if (c <= 0) break;
		l += c;
		d = realloc(d, l);
		if (!d) return 0;
		memcpy(d+p, buf, c);
		p += c;
	}
	*len = l;
	return d;
}

static byte *inf_buf;
static int inf_pos, inf_len;
static char* loader_error;

char* loader_get_error(void)
{
	return loader_error;
}

void loader_set_error(char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	loader_error = strdup(buf);
}

static int inflate_callback(byte b)
{
	if (inf_pos >= inf_len)
	{
		inf_len += 512;
		inf_buf = realloc(inf_buf, inf_len);
		if (!inf_buf) {
			loader_set_error("out of memory inflating file @ %d bytes\n", inf_pos);
			return -1;
		}
	}
	inf_buf[inf_pos++] = b;
	return 0;
}

typedef int (*unzip_or_inflate_func) (const unsigned char *data, long *p, int (* callback) (unsigned char d));

static byte *gunzip_or_inflate(byte *data, int *len, unsigned offset,
	unzip_or_inflate_func func)
{
	long pos = 0;
	inf_buf = 0;
	inf_pos = inf_len = 0;
	if (func(data+offset, &pos, inflate_callback) < 0)
		return data;
	free(data);
	*len = inf_pos;
	return inf_buf;
}

static byte *gunzip(byte *data, int *len) {
	return gunzip_or_inflate(data, len, 0, unzip);
}

/* primitive pkzip decompressor. it can only decompress the first
   file in a zip archive. */
static byte *pkunzip(byte *data, int *len) {
	unsigned short fnl, el, comp;
	unsigned int st;
	void *new;
	size_t newlen;
	int oldlen = *len;
	if (*len < 128) return data;
	memcpy(&comp, data+8, 2);
	comp = (unsigned short)(LIL((unsigned int)comp) & 0xFFFF);
	if(comp != 0 && comp != 8) return data;
	memcpy(&fnl, data+26, 2);
	memcpy(&el, data+28, 2);
	fnl = (unsigned short)(LIL((unsigned int)fnl) & 0xFFFF);
	el = (unsigned short)(LIL((unsigned int)el) & 0xFFFF);
	st = 30 + fnl + el;
	if(*len < (int)st) return data;
	if(comp == 0) {
		inf_buf = realloc(NULL, *len - st);
		memcpy(inf_buf, data+st, *len - st);
		free(data);
		inf_len = *len = *len - st;
		return inf_buf;
	}
	*len -= st;
	newlen = 0;
	new = tinfl_decompress_mem_to_heap(data+st, *len, &newlen, 0);
	if(new) {
		*len = (int)newlen;
		free(data);
		return new;
	}
	*len = oldlen;
	return data;
}

static int write_dec(byte *data, int len) {
	int i;
	for(i=0; i < len; i++)
		if(inflate_callback(data[i])) return -1;
	return 0;
}

static int unxz(byte *data, int len) {
	struct xz_buf b;
	struct xz_dec *s;
	enum xz_ret ret;
	unsigned char out[4096];

	/*
	 * Support up to 64 MiB dictionary. The actually needed memory
	 * is allocated once the headers have been parsed.
	*/
	s = xz_dec_init(XZ_DYNALLOC, 1 << 26);
	if(!s) goto err;

	b.in = data;
	b.in_pos = 0;
	b.in_size = len;
	b.out = out;
	b.out_pos = 0;
	b.out_size = sizeof(out);

	while (1) {
		ret = xz_dec_run(s, &b);
		if(b.out_pos == sizeof(out)) {
			if(write_dec(out, sizeof(out))) goto err;
			b.out_pos = 0;
		}

		if(ret == XZ_OK) continue;

		if(write_dec(out, (int)b.out_pos)) goto err;

		if(ret == XZ_STREAM_END) {
			xz_dec_end(s);
			return 0;
		}
		goto err;
	}

	err:
	xz_dec_end(s);
	return -1;
}

static byte *do_unxz(byte *data, int *len) {
	xz_crc32_init();
	xz_crc64_init();
	inf_buf = 0;
	inf_pos = inf_len = 0;
	if (unxz(data, *len) < 0)
		return data;
	free(data);
	*len = inf_pos;
	return inf_buf;
}

static byte *decompress(byte *data, int *len)
{
	if (data[0] == 0x1f && data[1] == 0x8b)
		return gunzip(data, len);
	if (data[0] == 0xFD && !memcmp(data+1, "7zXZ", 4))
		return do_unxz(data, len);
	if (data[0] == 'P' && !memcmp(data+1, "K\03\04", 3))
		return pkunzip(data, len);
	return data;
}

static FILE* rom_loadfile(char *fn, byte** data, int *len) {
	FILE *f;
#if defined(_MSC_VER) && __STDC_WANT_SECURE_LIB__
	if (strcmp(fn, "-")) fopen_s(&f, fn, "rb");
#else
	if (strcmp(fn, "-")) f = fopen(fn, "rb");
#endif
	else f = stdin;
	if (!f) {
	err:
		loader_set_error("cannot open rom file: %s\n", fn);
		return f;
	}
	*data = loadfile(f, len);
	if (!*data) {
		fclose(f);
		f = 0;
		goto err;
	}
	*data = decompress(*data, len);
	return f;
}

int bootrom_load() {
	byte *data;
	int len;
	FILE *f;
	REG(RI_BOOT) = 0xff;
	if (!bootroms[hw.cgb] || !bootroms[hw.cgb][0]) return 0;
	f = rom_loadfile(bootroms[hw.cgb], &data, &len);
	if(!f) return -1;
	bootrom.bank = realloc(data, 16384);
	memset(bootrom.bank[0]+len, 0xff, 16384-len);
	memcpy(bootrom.bank[0]+0x100, rom.bank[0]+0x100, 0x100);
	fclose(f);
	REG(RI_BOOT) = 0xfe;
	return 0;
}

/* memory allocation breakdown:
   loadfile returns local buffer retrieved via realloc()
   it's called only by rom_loadfile.
     rom_loadfile is called by bootrom_load and rom_load.
      bootrom_load is called once per romfile load via loader_init from
      load_rom_and_rc, and mem ends up in bootrom.bank, loader_unload() frees it.
      rom_load is called by rom_load_simple and loader_init().
       rom_load_simple is only called by rominfo in main.c, which we can ignore.
       the mem allocated by loadfile thru loader_init/rom_load ends up in
       rom.bank, which is freed in loader_unload(), just like the malloc'd
       rom.sbank.
   where it gets complicated is when rom_loadfile uncompresses data.
   the allocation returned by loadfile is passed to decompress().
   if it fails, it returns the original loadfile allocation, on success
   it returns a pointer to inf_buf which contains the uncompressed data.
*/

int rom_load()
{
	FILE *f;
	byte c, *data, *header;
	int len = 0, rlen;
	f = rom_loadfile(romfile, &data, &len);
	if(!f) return -1;
	header = data;

	memcpy(rom.name, header+0x0134, 16);
	if (rom.name[14] & 0x80) rom.name[14] = 0;
	if (rom.name[15] & 0x80) rom.name[15] = 0;
	rom.name[16] = 0;

	c = header[0x0147];
	mbc.type = mbc_table[c];
	mbc.batt = (batt_table[c] && !nobatt) || forcebatt;
	rtc.batt = rtc_table[c];
	mbc.romsize = romsize_table[header[0x0148]];
	mbc.ramsize = ramsize_table[header[0x0149]];

	if (!mbc.romsize) {
		loader_set_error("unknown ROM size %02X\n", header[0x0148]);
		return -1;
	}
	if (!mbc.ramsize) {
		loader_set_error("unknown SRAM size %02X\n", header[0x0149]);
		return -1;
	}

	rlen = 16384 * mbc.romsize;

	c = header[0x0143];

	/* from this point on, we may no longer access data and header */
	rom.bank = realloc(data, rlen);
	if (rlen > len) memset(rom.bank[0]+len, 0xff, rlen - len);

	ram.sbank = malloc(8192 * mbc.ramsize);

	initmem(ram.sbank, 8192 * mbc.ramsize);
	initmem(ram.ibank, 4096 * 8);

	mbc.rombank = 1;
	mbc.rambank = 0;

	hw.cgb = ((c == 0x80) || (c == 0xc0)) && !forcedmg;
	hw.gba = (hw.cgb && gbamode);

	if (strcmp(romfile, "-")) fclose(f);

	return 0;
}

int rom_load_simple(char *fn) {
	romfile = fn;
	return rom_load();
}

int sram_load()
{
	FILE *f;

	if (!mbc.batt || !sramfile || !*sramfile) return -1;

	/* Consider sram loaded at this point, even if file doesn't exist */
	ram.loaded = 1;

#if defined(_MSC_VER) && __STDC_WANT_SECURE_LIB__
	fopen_s(&f, sramfile, "rb");
#else
	f = fopen(sramfile, "rb");
#endif

	if (!f) return -1;
	fread(ram.sbank, 8192, mbc.ramsize, f);
	fclose(f);
	
	return 0;
}


int sram_save()
{
	FILE *f;

	/* If we crash before we ever loaded sram, DO NOT SAVE! */
	if (!mbc.batt || !sramfile || !ram.loaded || !mbc.ramsize)
		return -1;
	
#if defined(_MSC_VER) && __STDC_WANT_SECURE_LIB__
	fopen_s(&f, sramfile, "wb");
#else
	f = fopen(sramfile, "wb");
#endif

	if (!f) return -1;
	fwrite(ram.sbank, 8192, mbc.ramsize, f);
	fclose(f);
	
	return 0;
}


void state_save(int n)
{
	FILE *f;
	char *name;

	if (n < 0) n = saveslot;
	if (n < 0) n = 0;
	name = malloc(strlen(saveprefix) + 5);
	snprintf(name, strlen(saveprefix) + 5, "%s.%03d", saveprefix, n);

#if defined(_MSC_VER) && __STDC_WANT_SECURE_LIB__
	fopen_s(&f, name, "wb");
	if (f != NULL)
#else
	if ((f = fopen(name, "wb")))
#endif
	{
		savestate(f);
		fclose(f);
	}
	free(name);
}


void state_load(int n)
{
	FILE *f;
	char *name;

	if (n < 0) n = saveslot;
	if (n < 0) n = 0;
	name = malloc(strlen(saveprefix) + 5);
	snprintf(name, strlen(saveprefix) + 5, "%s.%03d", saveprefix, n);

#if defined(_MSC_VER) && __STDC_WANT_SECURE_LIB__
	fopen_s(&f, name, "rb");
	if (f != NULL)
#else
	if ((f = fopen(name, "rb")))
#endif
	{
		loadstate(f);
		fclose(f);
		vram_dirty();
		pal_dirty();
		sound_dirty();
		mem_updatemap();
	}
	free(name);
}

void rtc_save()
{
	FILE *f;
	if (!rtc.batt) return;
#if defined(_MSC_VER) && __STDC_WANT_SECURE_LIB__
	fopen_s(&f, rtcfile, "wb");
	if (f == NULL) return;
#else
	if (!(f = fopen(rtcfile, "wb"))) return;
#endif
	rtc_save_internal(f);
	fclose(f);
}

void rtc_load()
{
	FILE *f;
	if (!rtc.batt) return;
#if defined(_MSC_VER) && __STDC_WANT_SECURE_LIB__
	fopen_s(&f, rtcfile, "r");
	if (f == NULL) return;
#else
	if (!(f = fopen(rtcfile, "r"))) return;
#endif
	rtc_load_internal(f);
	fclose(f);
}

#define FREENULL(X) do { free(X); X = 0; } while(0)
void loader_unload()
{
	sram_save();
	if (romfile) FREENULL(romfile);
	if (sramfile) FREENULL(sramfile);
	if (saveprefix) FREENULL(saveprefix);
	if (rom.bank) FREENULL(rom.bank);
	if (ram.sbank) FREENULL(ram.sbank);
	if (bootrom.bank) FREENULL(bootrom.bank);
	mbc.type = mbc.romsize = mbc.ramsize = mbc.batt = 0;
}

static char *base(char *s)
{
	char *p;
	p = strrchr(s, '/');
	if (p) return p+1;
	return s;
}

static char *ldup(char *s)
{
	int i;
	char *n, *p;
	p = n = malloc(strlen(s));
	for (i = 0; s[i]; i++) if (isalnum((int)s[i])) *(p++) = tolower(s[i]);
	*p = 0;
	return n;
}

static void cleanup(void)
{
	sram_save();
	rtc_save();
	/* IDEA - if error, write emergency savestate..? */
}

int loader_init(char *s)
{
	char *name, *p;

	sys_checkdir(savedir, 1); /* needs to be writable */

	romfile = s;
	if(rom_load()) return -1;
	bootrom_load();
	vid_settitle(rom.name);
	if (savename && *savename)
	{
		if (savename[0] == '-' && savename[1] == 0)
			name = ldup(rom.name);
		else name = strdup(savename);
	}
	else if (romfile && *base(romfile) && strcmp(romfile, "-"))
	{
		name = strdup(base(romfile));
		p = strchr(name, '.');
		if (p) *p = 0;
	}
	else name = ldup(rom.name);

	saveprefix = malloc(strlen(savedir) + strlen(name) + 2);
	snprintf(saveprefix, strlen(savedir) + strlen(name) + 2, "%s/%s", savedir, name);

	sramfile = malloc(strlen(saveprefix) + 5);
	strncpy(sramfile, saveprefix, strlen(saveprefix) + 5);
	strncat(sramfile, ".sav", strlen(saveprefix) + 5);

	rtcfile = malloc(strlen(saveprefix) + 5);
	strncpy(rtcfile, saveprefix, strlen(saveprefix) + 5);
	strncat(rtcfile, ".rtc", strlen(saveprefix) + 5);

	sram_load();
	rtc_load();

	atexit(cleanup);
	return 0;
}

rcvar_t loader_exports[] =
{
	RCV_STRING("bootrom_dmg", &bootroms[0], "bootrom for DMG games"),
	RCV_STRING("bootrom_cgb", &bootroms[1], "bootrom for CGB games"),
	RCV_STRING("savedir", &savedir, "save directory"),
	RCV_STRING("savename", &savename, "base filename for saves"),
	RCV_INT("saveslot", &saveslot, "which savestate slot to use"),
	RCV_BOOL("forcebatt", &forcebatt, "save SRAM even on carts w/o battery"),
	RCV_BOOL("nobatt", &nobatt, "never save SRAM"),
	RCV_BOOL("forcedmg", &forcedmg, "force DMG mode for CGB carts"),
	RCV_BOOL("gbamode", &gbamode, "simulate cart being used on a GBA"),
	RCV_INT("memfill", &memfill, ""),
	RCV_INT("memrand", &memrand, ""),
	RCV_END
};









