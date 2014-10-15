#include <sys/fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ID3V2HDRSIZE	10

#define MP3SYNCWORD	0xFFE00000
#define MP3VERSION	0x00180000
#define MP3LAYER	0x00060000
#define MP3ERROR	0x00010000
#define MP3BITRATE	0x0000F000
#define MP3FREQ		0x00000C00
#define MP3PAD		0x00000200
#define MP3PRIV		0x00000100
#define MP3MODE		0x000000C0
#define MP3MODEEXT	0x00000030
#define MP3COPY		0x00000008
#define MP3ORIG		0x00000004
#define MP3EMPHASIS	0x00000003

#define MP3LAYER1	0x00060000
#define MP3LAYER2	0x00040000
#define MP3LAYER3	0x00020000

#define MP3MODEEXTMS		0x00000020
#define MP3MODEEXTINTENSITY	0x00000010

int mp3bitrates[16] = { 0, 32000, 40000, 48000, 56000, 64000, 80000, 96000, 112000, 128000, 160000, 192000, 224000, 256000, 320000};
int mp3samplingrates[3] = {44100, 48000, 32000};

int vflag;
int dflag;
int cflag;

char *ProgName;
char *FileName;

#define dprintf if (dflag) fprintf

// www.id3.org/id3v2.3.0 

main(argc, argv)
	int argc;
	char **argv;
{	FILE *mp3file;
	unsigned char id3v2hdr[ID3V2HDRSIZE];
	int tagsize = 0, framelength, id3v1tag;
	int ch, tflag;
	long mp3fileend, curseekptr;

	extern char *optarg;
	extern int optind, optopt, opterr, optreset;

	void mp3compare(), usage(), id3v2tag();
	int  hasid3v1tag(), convertsize(), mp3frame();

	vflag = tflag = 0;
	ProgName=argv[0];
	while ((ch = getopt(argc, argv, "cdtv?")) != -1) {
		switch (ch) {
		// print id3v2 flags
		case 't':
			tflag = 1;
			break;
		// verbose
		case 'v':
			vflag = 1;
			break;
		// print debugging stuff
		case 'd':
			dflag++;
			break;
		// emit md5 checksum of mp3 content
		case 'c':
			cflag++;
			break;
		// help
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1) {
		usage();
		exit(1);
	}

	FileName = *argv;

	// has id3v1 trailer?
	id3v1tag = hasid3v1tag(*argv);

	if ((mp3file = fopen(*argv, "r")) == NULL) {
		perror(*argv);
		exit(1);
	}

	// get file size with fseek instead of stat.  meh.
	if (fseek(mp3file, 0L, SEEK_END) != 0 || (mp3fileend = ftell(mp3file)) == -1) {
		perror(*argv);
		exit(1);
	}

	dprintf(stderr, "%s has size %ld\n", *argv, mp3fileend);
	rewind(mp3file);

	if (id3v1tag) {		// id3v1 tag is 128 byte trailer
		mp3fileend -= 128;
		dprintf(stderr, "%s has effective size %ld\n", *argv, mp3fileend);
	}

	// has variable length id3v2 header tags?
	fread(id3v2hdr, ID3V2HDRSIZE, 1, mp3file);
	if (memcmp(id3v2hdr, "ID3", 3) != 0) {
		fseek(mp3file, 0, SEEK_SET);	// no id3v2 tags, so mp3 starts at 0
	} else {
		dprintf(stderr, "ID3v2.%d.%d\n", id3v2hdr[3], id3v2hdr[4]);
		dprintf(stderr, "flags %x\n", id3v2hdr[5]);
		tagsize = convertsize(id3v2hdr+6);
		dprintf(stderr, "size %d\n", tagsize);
		if (id3v2hdr[5] != 0) {
			printf("nonzero ID3V2 flag\n");
			exit(1);
		}

		if (tagsize > 1000000) {	// ruh roh ...
			fprintf(stderr, "unlikely ID3V2 tag size (%d)\n", tagsize);
			exit(1);
		}

		dprintf(stderr, "tagsize %d\n", tagsize);
		if (tflag) {	// print the id3v2 tags
			id3v2tag(mp3file, tagsize);
		} else {	// skip 'em
			if (fseek(mp3file, tagsize, SEEK_CUR) != 0) {
				perror("fseek");
				exit(1);
			}
		}
	}

	curseekptr = ftell(mp3file);	// where am i?

	if (cflag) {	// checksum option
		dprintf(stderr, "%ld %ld %s\n", curseekptr, mp3fileend-curseekptr, *argv);
		mp3compare(mp3file, curseekptr, mp3fileend-curseekptr);
		exit(0);
	}

	dprintf(stderr, "start: %ld, length: %ld\n", curseekptr, mp3fileend-curseekptr);

	// painstakingly and pointlessly check mp3 frames
	while ((framelength = mp3frame(mp3file)) > 4) {
		if (curseekptr+framelength > mp3fileend) {
			dprintf(stderr, "incomplete frame: curseekptr: %ld, framelength: %d, mp3fileend: %ld, %ld left\n",
				curseekptr, framelength, mp3fileend, mp3fileend-curseekptr);
			break;
		}
		if (fseek(mp3file, framelength-sizeof(unsigned int), SEEK_CUR) != 0) {
			perror("fseek");
			exit(1);
		}
		curseekptr = ftell(mp3file);
		dprintf(stderr, "seek ptr is %ld\n", curseekptr);
		if (curseekptr == mp3fileend)
			break;
	}
	exit(0);
}

/*
 * AAAAAAAA AAABBCCD EEEEFFGH IIJJKLMM
 * A Frame sync (11)
 * B MPEG Audio version ID (2)
 * C Layer description (2)
 * D Protection bit (1)
 * E Bitrate index (4)
 * F Sampling rate frequency index (2)
 * G Padding bit (1)
 * H Private bit (1)
 * I Channel Mode (2)
 * J Mode extension (2)
 * K Copyright (1)
 * L Original (1)
 * M Emphasis (2)
 */

int
mp3frame(mp3file)
	FILE *mp3file;
{	unsigned int mp3hdr;
	int samplingrate, hascrc, bitrate, padded, framelength;

	dprintf(stderr, "-> mp3frame\n");

	if (fread(&mp3hdr, sizeof mp3hdr, 1, mp3file) != 1) {
		perror("fread");
		return -1;
	}

	mp3hdr = htonl(mp3hdr);

	dprintf(stderr, "mp3hdr 0x%x\n", mp3hdr);

	// A
	dprintf(stderr, "A: %8x\n", mp3hdr & MP3SYNCWORD);
	if ((mp3hdr & MP3SYNCWORD) != MP3SYNCWORD) {
		dprintf(stderr, "out of sync\n");
		return -1;
	}

	// B
	dprintf(stderr, "B: %8x\n", mp3hdr & MP3VERSION);
	if ((mp3hdr & MP3VERSION) != MP3VERSION) {
		dprintf(stderr, "MPEG2\n");
		return -1;
	}

	// C
	dprintf(stderr, "C: %8x\n", mp3hdr & MP3LAYER);
	switch (mp3hdr & MP3LAYER) {
		case MP3LAYER1:	dprintf(stderr, "MPEG1 layer 1\n"); return -1;
		case MP3LAYER2:	dprintf(stderr, "MPEG1 layer 2\n"); return -1;
		case MP3LAYER3:	break; /* mp3 == MPEG1 layer 3 */
		default:	dprintf(stderr, "unknown layer\n"); return -1;
	}

	// D
	dprintf(stderr, "D: %8x\n", mp3hdr & MP3ERROR);
	if (hascrc = ((mp3hdr & MP3ERROR) != MP3ERROR)) { /* 0: protected */
		dprintf(stderr, "CRC\n");	/* we can handle this */
		return -1;
	}

	// E
	dprintf(stderr, "E: %8x\n", mp3hdr & MP3BITRATE);
	bitrate = (mp3hdr & MP3BITRATE) >> 12;
	dprintf(stderr, "bitrate %X\n", bitrate);
	if (bitrate < 0 || bitrate > 14) {
		dprintf(stderr, "bit rate 0x%x is invalid\n", bitrate);
		return -1;
	}
	if (bitrate == 0)
		printf("free format");
	else
		printf("%d kbps", mp3bitrates[bitrate]/1000);

	// F
	printf(", sampling rate ");
	samplingrate = (mp3hdr & MP3FREQ) >> 10;
	if (samplingrate < 0 || samplingrate > 3) {
		printf("0x%x is invalid\n", samplingrate);
		exit(1);
	}
	if (samplingrate == 3)
		printf("reserved, ");
	else
		printf("%d Hz, ", mp3samplingrates[samplingrate]);

	// G
	dprintf(stderr, "G: %8x\n", mp3hdr & MP3PAD);
	padded = ((mp3hdr & MP3PAD) == MP3PAD);
	dprintf(stderr, "%spadded\n", padded ? "" : "not ");

	// H

	// I J
	switch ((mp3hdr & MP3MODE) >> 6) {
	case 0:		printf("stereo, "); break;
	case 1:		printf("joint stereo ");
			printf("(intensity ");
			if (((mp3hdr & MP3MODEEXTINTENSITY) >> 4) == 0)
				printf("off");
			else
				printf("on");
			printf(", MS ");
			if (((mp3hdr & MP3MODEEXTMS) >> 5) == 0)
				printf("off), ");
			else
				printf("on), ");
			break;
	case 2:		printf("dual channel, "); break;
	case 3:		printf("mono, "); break;
	}

	// K
	if ((mp3hdr & MP3COPY) == 0)
		printf("not ");
	printf("copyright, ");

	// L
	if ((mp3hdr & MP3ORIG) == 0)
		printf("not ");
	printf("original, ");

	// M
	switch (mp3hdr & MP3EMPHASIS) {
	case 0:	printf("no"); break;
	case 1: printf("50/15 ms"); break;
	case 2: printf("reserved"); break;
	case 3: printf("CCIT J.17"); break;
	}
	printf(" emphasis\n");
	
	if (hascrc) {
		if (fseek(mp3file, 2, SEEK_CUR) != 0) {
			perror("fseek");
			exit(1);
		}
	}

	printf("bitrate %d, mp3bitrate %d kbps, samplingrate %d, mp3samplingrate %d\n", bitrate, mp3bitrates[bitrate]/1000, samplingrate, mp3samplingrates[samplingrate]);

	framelength = (144 * mp3bitrates[bitrate]/mp3samplingrates[samplingrate]) + padded;

	dprintf(stderr, "frame length: %d, padded: %d\n", framelength, padded);
	return framelength;
}

void
id3v2tag(mp3file, tagsize)
	FILE *mp3file;
	int tagsize;
{	unsigned char *tag, *tptr;
	int i, framesize;

	int convertsize();
	extern int isprint();

	tag = malloc(tagsize);
	fread(tag, tagsize, 1, mp3file);

	tptr = tag;
	while (tptr < tag + tagsize) {
		if (tptr[0] == 0 &&  tptr[1] == 0 && tptr[2] == 0 && tptr[3] == 0)
			break;
		framesize = convertsize(tptr + 4);
		printf("%c%c%c%c (size %d, flag %x%x)\n", tptr[0], tptr[1], tptr[2], tptr[3], framesize, tptr[8], tptr[9]);
		tptr += 10;

		if (vflag == 0)
			tptr += framesize;
		else {
			for (i = 0; i < framesize; i++) {
				if (isprint(*tptr))
					putchar(*tptr);
				else
					printf("0x%x", *tptr);
				tptr++;
			}	
			putchar('\n');
		}
	}
	free(tag);
}

int
convertsize(insize)
	unsigned char *insize;
{	int i, outsize = 0;
	
	for (i = 0; i < 4; i++) {
		outsize = (outsize << 7);
		outsize |= (insize[i] & 0x7f);
	}
	return outsize;
}

void
usage()
{
	fprintf(stderr, "usage: %s [-cdtv] file\n", ProgName);
	exit(1);
}

int
hasid3v1tag(f)
	char *f;
{	int fd, hastag;
	char tag[128];

      if ((fd = open(f, O_RDONLY)) < 0) {
                perror("open");
		exit(-1);
        }

        if (lseek(fd, -128, SEEK_END) < 0) {
                perror("lseek");
		exit(-1);
        }

        if (read(fd, &tag, 128) != 128) {
                perror("read");
		exit(-1);
        }

	if (close(fd) < 0) {
		perror("close");
		exit(-1);
	}

	hastag = (strncmp(tag, "TAG", 3) == 0);
	dprintf(stderr, "%s %s ID3v1 trailer\n", f, hastag ? "has" : "does not have");

	return hastag;
}

#define MD5PATH "/sbin/md5"
#define BUFSIZE 4096

void
mp3compare(mp3file, start, len)
	FILE *mp3file;
	long start, len;
{	FILE *md5write;
	int p2c[2], c2p[2], childpid;
	char buf[BUFSIZE], md5string[80], *newline;

	if (pipe(p2c) < 0) {
		perror("pipe");
		exit(-1);
	}
	if (pipe(c2p) < 0) {
		perror("pipe");
		exit(-1);
	}

	switch(fork()) {
	case -1:	
		if ((childpid = fork()) == -1) {
			perror("fork");
			exit(-1);
		}

	case 0:      // this is the child.
		close(p2c[1]);	
		close(c2p[0]);

		if (dup2(p2c[0], 0) != 0) {
			perror("child dup2 fd 0");
			_exit(-1);
		}
		if (dup2(c2p[1], 1) != 1) {
			perror("child dup2 fd 1");
			_exit(-1);
		}
		
		execl(MD5PATH, "md5", (char *) 0);
		perror("execl");
		_exit(-1);
	}

	// this is the parent
	close(p2c[0]);	
	close(c2p[1]);

	if ((md5write = fdopen(p2c[1], "w")) == NULL) {
		perror("fdopen");
		exit(-1);
	}

	if (fseek(mp3file, start, SEEK_SET) != 0) {
		perror(FileName);
		exit(-1);
	}

	for ( ; len >= BUFSIZE; len -= BUFSIZE) {
		if (fread(buf, BUFSIZE, 1, mp3file) != 1) {
			perror(FileName);
			exit(-1);
		}
		if (fwrite(buf, BUFSIZE, 1, md5write) != 1) {
			perror("parent pipe write");
			exit(-1);
		}
	}

	if (len > 0) {
		if (fread(buf, len, 1, mp3file) != 1) {
			perror(FileName);
			exit(-1);
		}
		if (fwrite(buf, len, 1, md5write) != 1) {
			perror("parent pipe write");
			exit(-1);
		}
	}

	if (fclose(md5write) != 0) {
		perror("parent fclose");
		exit(-1);
	}

	if (read(c2p[0], md5string, 80) < 0) {
		perror("parent read md5 string");
		exit(-1);
	}

	if ((newline = index(md5string, '\n')) != NULL)
		*newline = 0;
	printf("%s %s\n", md5string, FileName);
}
