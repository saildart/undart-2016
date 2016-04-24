#if 0
echo Self compiling source on machine type $(uname -m)
if [ $(uname -m) == 'x86_64' ];then M=-m64;else M='';fi
echo gcc -g $M -Wall -Werror -o /usr/local/bin/undart main.c md5.c
time gcc -g $M -Wall -Werror  -o /usr/local/bin/undart main.c md5.c
return
#endif

/* undart-2016.c
 * 2016 notes
 * =======================================================================================
   I am now on Linux distribution Ubuntu 15.04 with XFCE not Unity. The uname -a says:
        Linux work8 3.19.0-20-generic #20-Ubuntu SMP
        Fri May 29 10:10:47 UTC 2015
        x86_64 x86_64 x86_64 GNU/Linux
   last year it was
        Linux work15 35-generic #62-Ubuntu SMP 
        Fri Aug 15 01:58:42 UTC 2014
        x86_64 x86_64 x86_64 GNU/Linux

The SAIDART blob serial numbering may be considered as
the 'Accession Numbers' for the collection of SAILDART
as if it were a Digital Libary.

For my 2015 simplified SETUP of a TARGET disk,
        the disk today happens to be at /dev/sdd1
        so mount point /d and generic LINUX standard file system ext4
        without my usual LOOP mounts and RAID1 mechanism.

# Initialize file system on large disk ( 2 TiB )
        # Was: echo ,,L | sfdisk /dev/sdf
        # new this year
        sgdisk -Z /dev/sdd
        gdisk ENTER then commands 'n' and 'w' take all the defaults to make one partition
        mkfs.ext4 /dev/sdd1
        mount /dev/sdd1 /d
# Make TARGET directories
        mkdir -p /d/2015/{csv,dart,data13/sn,data7/sn,log,tbz}
        chown bgb:bgb -R /d/
# Link absolute pathnames to point at the new TARGET file system directories
        sudo bash
        ln -s /d/2015/data7     /data7
        ln -s /d/2015/data13    /data13
# Unpack the compressed DART records from the 229 reels of tape ... 17m34s
# yielding 41_594 dart records
        cd /d/2015/dart
        time tar -xf /d/2015/lzma/dart_records_from_229_reels.tar.lzma
# run the UNDART .. took 75m 25 seconds
`2014-09-12 bgbaumgart@mac.com'

`2015-06-16 bgbaumgart@mac.com'

#
# Short test, undart.
#
        cd /dartrecords
        undart p3000.00?
        # ASSERT 356 sn files created
	# ASSERT file /data7/sn/000030 is FORTUN.TXT with 119 fortune cookie lines.
# Check that $(head -1 /data7/sn/000030) yields the fortune cookie:
# "You will soon meet a person who will play an important role in your life."

#
# Full run, undart. Single process.
#
        cd /dartrecords
        # ASSERT readdir .|wc -l is 41594
        time undart p*
#
# 2015 Maria DB replacing Oracle Mysql, using the ubuntu 15.04 package
#
        apt-get install mariadb-server
        mysql_secure_installation
        mysql    -h localhost -u root mysql -p PASSWORD_FOR_DB_ROOT
        CREATE USER 'bgb'@'localhost' IDENTIFIED BY 'foo99baz';
        CREATE DATABASE saildart CHARACTER SET UTF8;
        GRANT ALL ON saildart.* TO 'bgb'@'localhost';
#
# Load DART meta from CSV files and do some SQL scripting
#
        1st-create-dart.sql
        2nd-create-snhash-and-mfd.sql
        3rd-create-people.sql
#
# data conversions from PAST blobs to PRESENT formats
#

#
# creat static web site into Lcorpus
#
        ln -s /Lcorpus/static           /static
        ln -s /Lcorpus/static/html      /html
        ln -s /Lcorpus/content          /content
#
# 
#
 * =======================================================================================
 * copyright:(c)Ⓒ2001-2015 Bruce Guenther Baumgart
 * software_license:  GPL license Version 3.
 * http://www.gnu.org/licenses/gpl.txt MD5=393a5ca445f6965873eca0259a17f833
 * =======================================================================================
 */

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <assert.h>
#include "md5.h"

typedef unsigned long long uint64;
typedef          long long  int64;

/* PDP-10 word formats (little endian for Intel and AMD x86 architecture)
 * ----------------------------------------------------------------------
 * The PDP-6 and the PDP-10 are BIG endian. Network order is BIG endian.
 * The PDP-11, Intel and AMD are LITTLE endian. x86 ( 32bit i686 and 64bit x86_64 ).
 * 
 * Union and 'C' bit field struct are endian dependent.
 * This union is only good on little endian machines.
 */
typedef union
{
  uint64 fw;
  char byte[8];
  struct { uint64 word:36,:28;				} full;
  struct {  int64 right:18,left:18,:28;         	} half;
  struct {  int64 right:18,x:4,i:1,a:4,op:9,:28;      	} instruction;
  struct { uint64 byte5:4, byte4:8, byte3:8, byte2:8, byte1:8,:28; } tape; // packing.
  struct { uint64 c6:6,c5:6,c4:6,c3:6,c2:6,c1:6,:28;	} sixbit; // ASCII sixbit+040
  struct { uint64 bit35:1,a5:7,a4:7,a3:7,a2:7,a1:7,:28;	} seven; // seven bit ASCII
  // WAITS file RIB "Retrieval Information Block" format
  struct { uint64 :15,date_hi:3,:46; } word2;
  struct { uint64 date_lo:12,time:11,mode:4,prot:9,:28; } word3;
  struct { uint64 count:22,:42; } word6;
  // HEAD TAIL block bit fields
  struct { uint64 date_lo:12,time:11,:9,prev_media_flag:1,date_hi:3,:28; } head_tail_word3;  
} pdp10_word;

// #define MAX_BYTE_COUNT 39168000  // Largest DART tape pdp10-file in bytes, so lets use 40 MB.
#define MAX_BYTE_COUNT 40000000  // Larger then all DART tape pdp10-file in bytes.
#define MAX_WORD_COUNT ((MAX_BYTE_COUNT+4)/5)
unsigned char data5[ MAX_BYTE_COUNT ];
pdp10_word    data8[ MAX_WORD_COUNT ]; // 64-bit words.

extern int errno;

/*
 * globals 
 */
        char *saildartroot = ""; /* was "/data/YEAR" */
        FILE *stdlog;
        FILE *stdcsv;
        FILE *snhash;

        char str[128];
        int tapeno=0;
        char dartreel[32];
        long long int byte_count, word_count;
        int bad_data_word=0;

        int waitsdate;	/* WAITS file date and time */
        int waitstime;	/* WAITS file date and time */
        int waitsprot;	/* WAITS file protection */
        int waitsmode;	/* WAITS file data mode */

        int sailprot;	/* WAITS file protection === SAIL prot of SYSTEM.DMP[J17,SYS] */

        char iso_datetime[64];
        int year, month, day, hour, minute, second;
        char device[7];
        char filnam[7], ext[4], prj[4], prg[4];

        int data_word_count; /* Number of 36-bit words in current file */

        int record_type;
        int record_length;
        int ma_old, length_old;
/*
 * MD5 one way hash, from the FSF coreutil package.
 */
        struct md5_ctx ctx;
        unsigned char data5_MD5_digest[16]; char data5_MD5_hash[32+1];
        unsigned char data7_MD5_digest[16]; char data7_MD5_hash[32+1];
        unsigned char data8_MD5_digest[16]; char data8_MD5_hash[32+1];
        int serial_number=0;
/*
 * File output buffers for present-day (circa 2010) formats.
 */
        unsigned long ofile_switch;
        unsigned long obuf8_word_count;
        unsigned long size5, size8;
        int ubyte_cnt, uchar_cnt;

        unsigned long long obuf8[ MAX_WORD_COUNT ];
	unsigned char      obuf5[ MAX_BYTE_COUNT ];
        unsigned char obuf_utf8[ 3*MAX_BYTE_COUNT ];
        unsigned char ucontent[ 3*MAX_BYTE_COUNT ]; /* utf8 back slashed for insert statement value */
        char ucommand[ 3*MAX_BYTE_COUNT ]; /* insert command */
/*
 * PDP-10 file statistics used to assign filetype TEXT when likely, otherwise BINARY.
 */
	long pdp10_words=0;
	long bit35_count=0; /* count words in file with BIT#35 on */
        long non_numeric_byte5_and_bit35 = 0; /* NOT possible in TEXT file, common in BINARY. */
        long pushj_count=0;     // PDP binary code criterion
	long crlf_count=0; /* count CR-LF two byte strings marking end-of-line of text */
	long nzbytes=0;         // count NON-ZERO byte characters in a data5 file
	char filetype='?';
        char Public='?';        // 'Y' or 'N' default is NO presume PRIVATE.
char copyright_hit='N'; // yes or no on whether or not the string 'copyright' appeared in the file

/* ascii SIXBIT decoding functions
 * -------------------------------------------------------------------- *
 * sixbit word into ASCII string 
 * sixbit halfword into ASCII string
 */
char *
sixbit_word_into_ascii(char *p0,pdp10_word w)
{
  char *p=p0;
  *p++ = (w.sixbit.c1 + 040);
  *p++ = (w.sixbit.c2 + 040);
  *p++ = (w.sixbit.c3 + 040);
  *p++ = (w.sixbit.c4 + 040);
  *p++ = (w.sixbit.c5 + 040);
  *p++ = (w.sixbit.c6 + 040);
  *p++= 0;
  return p0;
}
char *
sixbit_halfword_into_ascii(char *p0,int halfword)
{
  char *p=p0;
  *p++ = ((halfword >> 12) & 077) + 040;
  *p++ = ((halfword >> 6) & 077) + 040;
  *p++ = (halfword & 077) + 040;
  *p++ = 0;
  return p0;
}
/* Convert SAIL file WAITS protection bits into Unix file protection bits.
 * -----------------------------------------------------------------------
 */
int
waits2unixprot(unsigned int prot)
{
  prot &= ~0444;// Turn off meaningless bits
  prot <<= 1;	// Shift the WAITS "rw" bits into the correct UNIX places
  prot ^= 0666;	// Change polarity
  prot |= 0400; // ENABLE read for owner
  return(prot);
}

/* Convert 5-byte PDP-10 "tape-words" into 8-byte unsigned long long words. 
 * -------------------------------------------------------------------- *
 * The PDP-10 ( DEC System-20 ) tape words were 36-bits packed into bytes in
 * BIG Endian order with the final four bits in the low order of the fifth byte.
 * The 36-bit PDP-10 words are placed into the low order side of a 64-bit 
 * unsigned long long union so that bit field notation can be used for unpacking.
 */
void
convert_words_5bytes_to_8bytes( char *src_file ){
  int i,ma;
  /*
   * reset 
   */
  bzero(data5,sizeof(data5));
  bzero(data8,sizeof(data8));
  fflush(stdlog);
  fflush(stdcsv);
  /*
   * Input from source file 
   */
  errno = 0;
  i = open(src_file,O_RDONLY);
  if (i<0){
    fprintf(stderr,"ERROR: input source open file \"%s\" failed.\n",src_file);
    return;
  }   
  byte_count = read(i,data5,sizeof(data5));
  close(i);
  word_count = byte_count/5;
  if(0)
  fprintf(stderr,"%9s read n=%lld bytes, errno=%d, word_count=%lld=0%llo\n",
          src_file, byte_count,errno,word_count,word_count);
  fprintf(stdlog,"%9s read n=%lld bytes, errno=%d, word_count=%lld=0%llo\n",
          src_file, byte_count,errno,word_count,word_count);
  fflush(stdlog);
  assert( word_count < MAX_WORD_COUNT );
  for( ma=0; ma < word_count; ma++ ){
    int q = ma*5;
    data8[ma].tape.byte1 = data5[q];
    data8[ma].tape.byte2 = data5[q+1];
    data8[ma].tape.byte3 = data5[q+2];
    data8[ma].tape.byte4 = data5[q+3];
    data8[ma].tape.byte5 = data5[q+4];
  }
}
void
force_to_lowercase(char *p)
{
    for (;*p;p++)    
        if ('A'<=*p && *p<='Z')
            *p |= 040;
}

/* Replace ugly SIXBIT filename chararacters
 * -----------------------------------------
 * In filename SIXBIT there are 26 + 10 alphanumeric characters,
 * plus nine more characters that may appear in a SAILDART filename:
 *	hash		#       dollar		$
 *      percent		%       ampersand       &
 *      plus            +       minus           -
 *      equal sign      =
 *
 * Use capital letters in unix version of PDP-10 filenames to replace characters
 * that are ugly to use on unix GNU/Linux file systems.
 *      CHARACTER NAME		UGLY	Replacement
 *	--------------		----	-----------
 *	ampersand		'&'	A
 *	colon			':'	C
 *	space		        ' '	S
 *	bang			'!'	B
 *	double quote		'"'	Q
 *	left parens		'('	L left
 *	right parens		')'	R right
 *	left square bracket	'['	I like In
 *	right square bracket	']'	O like Out
 *	caret			'^'	V
 *	underbar		'_'	U
 *	asterisk(aka star)	'*'	X 
 *	comma			','	Y
 *      slash                   '/'     Z
 *      less-than               '<'     N thaN
 *      greater-than            '>'     M More
 *      question-mark           '?'     W Who-What-Where-When WTF
 *      back-slash              '\\'    K bacK
 *      dot                     '\.'    D
 *      semicolon               ';'     S
 *      apostrophe              "'"     P aPPostroPPhe dang FILNAM . LPT [SPL,SYS]
 */

void
replace_ugly_characters(char *p)
{
    char *q;
    char *ugly =   ";'" "#" "&" ":" "!" "\"" "()" "[]" "^" "_" "*" "," "/" "<>" "?" "\\" ".";
    char *pretty = "SP" "H" "A" "C" "B" "Q"  "LR" "IO" "V" "U" "X" "Y" "Z" "NM" "W" "K"  "D";
    assert( strlen(ugly) == strlen(pretty)); /* so check the character count */
    while ((q= strpbrk(p,ugly)))
    {
      *q = pretty[index(ugly,*q)-ugly]; /* Beautification by table lookup */
    }
}
void
omit_spaces(char *q)
{
  char *t,*p;
  for (p= t= q; *p; p++)
    if (*p != ' ')
      *t++ = *p;
  *t++ = 0;
}
void
unixname(char* flat, char* filnam, char* ext, char* prj, char* prg)
{
  char clean_filnam[16]={},clean_ext[16]={},clean_prj[16]={},clean_prg[16]={};
  bzero(clean_filnam,16);
  bzero(clean_ext,16);
  bzero(clean_prj,16);
  bzero(clean_prg,16);

    /* Convert UPPER to lower and replace ugly punctuation with UPPER codes */
    strncpy( clean_filnam, filnam, 6 );
    strncpy( clean_ext, ext, 3 );
    strncpy( clean_prj, prj, 3 );
    strncpy( clean_prg, prg, 3 );

    omit_spaces( clean_filnam );
    omit_spaces( clean_ext );
    omit_spaces( clean_prj );
    omit_spaces( clean_prg );

    force_to_lowercase( clean_filnam );
    force_to_lowercase( clean_ext );
    force_to_lowercase( clean_prj );
    force_to_lowercase( clean_prg );

    replace_ugly_characters( clean_filnam );
    replace_ugly_characters( clean_ext );

    /* UNIX like order: Programmer, Project, Filename, dot, Extension */
    sprintf( flat, "%.3s_%.3s_%.6s.%.3s", clean_prg, clean_prj, clean_filnam, clean_ext );

    /* Remove trailing dot, if extension is blank */
    
    if ( flat[strlen(flat)-1] == '.' ){
      flat[strlen(flat)-1] = 0;
    }
}

/* Find next good record header 
 * -------------------------------------------------------------------- *
 */
int
advance_to_good_record_header(int ma)
{
  int ma2, p_length;
  long long n, q;
  unsigned char *p;
  int ma_new;
  /**/
  bad_data_word++;
  if (ma == (ma_old+1)){
  ma += 2;
}
  assert( ma < MAX_WORD_COUNT );
  q = ma*5;

  // Scan for record marker \223 \72 \300 \0 \0 which is SIXBIT/DSK   /.
  p = data5 + q - 1;
  do {
  p++;
  n = byte_count - (p - data5);
  p = (unsigned char *)memchr((void *)p,'\223', n );
  p_length = p ? ((p[-3]&077)<<12) | (p[-2]<<4) | (p[-1]&0xF) : 0;
  //    p_DSK = p ? ((uint64)p[0]<<28 | p[1]<<20 | p[2]<<12 | p[3]<<4 | (0xF & p[4])) : 0;
} while (p && !( 
  p[0]==0223 && p[1]==072 && p[2]==0300 && p[3]==0 && p[4]==0  // sixbit 'DSK   '
    && p[-5]==0377 && p[-4]==0377 && (p[-3]&0300)==0100        // xwd -3,,length
    && p_length>=59
    && p_length<=10238
    ));
  if ( ! p ){
  // ABORT scan. No further good record marker found.
  return word_count; // Return next ma just beyond EOF.
}
  // repack into words starting from the possibly good byte position.
  p -= 5;
  ma_new = ((p+1) - data5) / 5;
  assert( ma_new < MAX_WORD_COUNT );
  for( ma2=ma_new; ma2 < word_count; ma2++ ){
  data8[ma2].tape.byte1 = *p++;
  data8[ma2].tape.byte2 = *p++;
  data8[ma2].tape.byte3 = *p++;
  data8[ma2].tape.byte4 = *p++;
  data8[ma2].tape.byte5 = *p++;
}
  ma_old = ma_new;
  length_old = p_length; // supposed length of newly found record.
  return ma_new; // continue scan.
}

/* This table provides a unicode 16-bit value for each SAIL 7-bit character. */
unsigned short sail_unicode[128]={
         0,
         0x2193, // ↓ down arrow
         0x03b1, // α alpha
         0x03b2, // β beta
         0x2227, // ∧ boolean AND
         0x00ac, // ¬ boolean NOT
         0x03b5, // ε epsilon
         0x03c0, // π pi
         0x03bb, // λ lambda
         011,    012,    013,    014,    015,   // TAB, LF, VT, FF, CR as white space
         0x221e, // ∞ infinity
         0x2202, // ∂ partial differential

         0x2282, 0x2283, 0x2229, 0x222a, // ⊂ ⊃ ∩ ∪ horseshoes left, right, up down.
         0x2200, 0x2203, 0x2297, 0x2194, // ∀ ∃ ⊗ ↔ forall, exists, xor, double arrow.
         0x005f, 0x2192, 0x007e, 0x2260, // _ → ~ ≠ underbar, right arrow, tilde, neq
         0x2264, 0x2265, 0x2261, 0x2228, // ≤ ≥ ≡ ∨ le, ge, equ, boolean OR

           040,041,042,043,044,045,046,047,
           050,051,052,053,054,055,056,057,
           060,061,062,063,064,065,066,067,
           070,071,072,073,074,075,076,077,

           0100,0101,0102,0103,0104,0105,0106,0107,
           0110,0111,0112,0113,0114,0115,0116,0117,
           0120,0121,0122,0123,0124,0125,0126,0127,
           0130,0131,0132,0133,0134,0135,
         
         0x2191, // ↑ up arrow
         0x2190, // ← left arrow

         0140,0141,0142,0143,0144,0145,0146,0147,
         0150,0151,0152,0153,0154,0155,0156,0157,
         0160,0161,0162,0163,0164,0165,0166,0167,
         0170,0171,0172,

         0x7b,          // { left curly bracket
         0x7c,          // | vertical bar
         0x2387,        // ⎇ altmode is a keystroke character glyph NOT a shift key.
         0x7d,          // } right curly bracket ASCII octal 0175, but SAIL code 0176 !

         0x2408         // Backspace unicode BS backspace glyph ␈u2408
};

/*
 * Initialize the UTF8 tables using the 16-bit unicode characters
 * defined in sail_unicode[128] corresponding to the 7-bit SAIL incompatible ASCII codes.
 */
char utf8_[128*4];      // content of utf strings
char *utf8[128];        // string pointers
int utf8size[128];      // string length
#define PUTCHAR(U)*p++=(U)
void
initialize_utf8_tables(){
  int i,u;
  char *p=utf8_;
  /**/
  for (i=0;i<128;i++){
    utf8[i] = p;
    u = sail_unicode[i];
    if ( 0x0001<=u && u<=0x007f ){
      utf8size[i]=1;
      PUTCHAR( u );
      PUTCHAR(0);
      PUTCHAR(0);
      PUTCHAR(0);
    } else
      if ( 0x00a0<=u && u<=0x07ff ){
        utf8size[i]=2;
        PUTCHAR(0xC0 | ((u>>6) & 0x1F));
        PUTCHAR(0x80 | ( u     & 0x3F));
        PUTCHAR(0);
        PUTCHAR(0);
      } else
	if ( 0x0800<=u && u<=0xffff ){
          utf8size[i]=3;
          PUTCHAR(0xE0 | ((u>>12) & 0x0F));
          PUTCHAR(0x80 | ((u>>6 ) & 0x3F));
          PUTCHAR(0x80 | ( u      & 0x3F));
          PUTCHAR(0);
        } else {
          utf8size[i]=0;
          PUTCHAR(0);
          PUTCHAR(0);
          PUTCHAR(0);
          PUTCHAR(0);          
        }
  }
}
#undef PUTCHAR

void
convert_data5_into_utf8(void){
  unsigned char *p=obuf_utf8;
  unsigned char c;
  int i,n;
  ubyte_cnt = 0;
  uchar_cnt = 0;
  for ( i=0; i < size5; i++ ){
    c = obuf5[i] & 0x7F;
    n = utf8size[c];
    if ( !n ) continue;
    strncpy( (char *)p, (char *)utf8[c], n );
    ubyte_cnt += n;
    uchar_cnt += 1;
    p += n;
    // Replace one CR-LF of output with one LF. Note input i continues.
    if ( i>0 && p[-2]==015 && p[-1]==012 ){
      uchar_cnt--;
      ubyte_cnt--;
      p[-2] = 012;
      p[-1] = 0;
      p--;
    }
  }
}

void
output_data7_file( int wrdcnt ){
  char data7_md5_name[120];
  char data7_sn_name[120];
  int i,o,n;
  char *p, *q;
  /**/
  convert_data5_into_utf8();
  md5_init_ctx( &ctx );
  md5_process_bytes( obuf_utf8, ubyte_cnt, &ctx );
  md5_finish_ctx( &ctx, data7_MD5_digest );
  p = (char *)data7_MD5_digest;
  q = data7_MD5_hash;
  for (i=0; i<16; i++)
    {
      *q++ = "0123456789abcdef"[(*p & 0xF0 ) >> 4 ];
      *q++ = "0123456789abcdef"[(*p++) & 0xF ];
    }
  sprintf( data7_md5_name,"%s/data7/md5/%s",saildartroot,data7_MD5_hash );
  sprintf( data7_sn_name,"%s/data7/sn/%06d",saildartroot,serial_number);
  errno = 0;
  if(!access( data7_sn_name, F_OK )){
    /* file content already exists. */
    errno = 0;
    return;
  }
  errno = 0;
  o = open( data7_sn_name, O_CREAT | O_WRONLY | O_EXCL, 0400 );
  if (!( o > 0)){
    fprintf(stderr,"Open failed data7 filename='%s'\n", data7_sn_name);
    /* ASSUME file content already exists (parallel processing) */
    errno=0;
    return;
  }
  assert( o > 0);
  n = write( o, obuf_utf8, ubyte_cnt );
  assert( n == ubyte_cnt );
  assert(!close( o ));
  //  assert(!link(data7_md5_name,data7_sn_name));
}

/*
 * Wrap the source string inside Double Quote marks and
 * do the back slashing of infixed single-quote, double-quite and slash. 
 */
void dq( char *dst, char *src ){
  *dst++ = '"';
  while( *src ){
    switch( *src ){
    case '"':
    case '\\':
    case '\'':
      *dst++ = '\\'; // backslash prefix for the csv special characters
    }
    *dst++ = *src++;
  }
  *dst++ = '"';
  *dst++ = 0;
}

// Poor man's database for looking up an accession serial number,
// given a content blob MD5 hash string.
 char *key[999999]; // Sorted MD5 hash strings of content blobs,
 long  val[999999]; // corresponding SN# serial number for each content blob.
long snmax=886464; // next sn
long
select_sn(char *hash){
  int h,j,k;
  j = k = ( snmax >>1 );
  // binary search
  while((k>0) && (h=strncmp(key[j],hash,32))){
    k >>= 1;
    if( h>0 ){ j -= k; }else
    if( h<0 ){ j += k; }else
      return val[j];
  }
  // linear search
  for(;0>(h=strncmp(key[j],hash,32));j++){}
  for(;0<(h=strncmp(key[j],hash,32));j--){}
  //  assert( h==0 );
  if(h != 0){
    int snval = snmax++; // mint coin a new serial number
    // whirr
    for( k=snval-2; strncmp(key[k],hash,32)>0; k--){
      key[k+1] = key[k];
      val[k+1] = val[k];
    }
    // plop
    key[k] = malloc(34);
    strncpy(key[k],hash,32);
    val[k] = snval;
    return snval;
  }
  return val[j];
}

void
output_data8_file(int wrdcnt){
  int i,o,n; char *p, *q;
  char data8_md5_name[120];
  char data8_sn_name[120];
  /*
   * hash content of output buffer into an MD5 value 
   */
  md5_init_ctx( &ctx );
  md5_process_bytes( obuf8, wrdcnt*8, &ctx );
  md5_finish_ctx( &ctx, data8_MD5_digest );
  p = (char *)data8_MD5_digest;
  q = data8_MD5_hash;
  for (i=0; i<16; i++)
    {
      *q++ = "0123456789abcdef"[(*p & 0xF0 ) >> 4 ];
      *q++ = "0123456789abcdef"[(*p++) & 0xF ];
    }
  sprintf(data8_md5_name,"%s/data8/md5/%s",saildartroot,data8_MD5_hash);
  sprintf(data8_sn_name,"%s/data8/sn/%06d",saildartroot,serial_number);
  obuf8_word_count = wrdcnt;
  size8 = obuf8_word_count * 8;
  errno=0;
  if(! access(data8_sn_name,F_OK) ){
    /* file content already exists */
    errno=0;
    return;
  }
  errno = 0;
  o = open( data8_sn_name, O_CREAT | O_WRONLY | O_EXCL, 0400 );
  if (!( o > 0)){
    fprintf(stderr,"Open failed filename data8_sn_name='%s'\n", data8_sn_name);
  }
  assert( o > 0);
  n = write( o, obuf8, size8 );
  assert( n == size8 );
  assert(!close( o ));
  //  assert(!link(data8_md5_name,data8_sn_name));
}

void
output_data13_file(int wrdcnt){
  char data13_sn_name[120];
  FILE *fd;
  int i;
  sprintf(data13_sn_name,"%s/data13/sn/%06d",saildartroot,serial_number);
  errno = 0;
  if(! access(data13_sn_name,F_OK) ){
    /* file content already exists */
    errno=0;
    return;
  }
  errno = 0;
  fd = fopen( data13_sn_name, "w" );
  if (errno){
    fprintf(stderr,"Open failed filename data13_sn_name='%s'\n", data13_sn_name);
    /* ASSUME file content already exists (parallel processing) */
    errno=0;
    return;
  }
  assert(!errno);
  for ( i=0; i<wrdcnt; i++ ){
    fprintf(fd,"%012llo\n",obuf8[i]);
  }
  assert(!fclose( fd ));
}

void
data_record_payload_to_output_buffers( int base, int seen, int wrdcnt ){
  int i;
  /*
   * Copy from input buffer to output buffers
   */
  for (i=0; i<wrdcnt; i++){
    int offset=5*(seen+i);
    /* data5 format */
    obuf5[offset] = data8[base+i].seven.a1;
    obuf5[offset+1] = data8[base+i].seven.a2;
    obuf5[offset+2] = data8[base+i].seven.a3;
    obuf5[offset+3] = data8[base+i].seven.a4;
    obuf5[offset+4] = data8[base+i].seven.a5 | (data8[base+i].seven.bit35 ? 0x80 : 0);
    /* data8 format */
    obuf8[seen+i] = data8[base+i].fw ;
  }
}

#define MAX_SNHASH 886463
unsigned char snhash5[999999];

int
output_data5_file(int wrdcnt){
  char *p, *q;
  int i;
  /* compute md5 hash of this content blob */
  md5_init_ctx( &ctx );
  md5_process_bytes( obuf5, wrdcnt*5, &ctx );
  md5_finish_ctx( &ctx, data5_MD5_digest );
  p = (char *)data5_MD5_digest;
  q = data5_MD5_hash;
  for (i=0; i<16; i++)
    {
      *q++ = "0123456789abcdef"[(*p & 0xF0 ) >> 4 ];
      *q++ = "0123456789abcdef"[(*p++) & 0xF ];
    }

  // In the EARLY YEARS of SAILDART (1998 to 2008) I renumbered the content lumps
  // each time a reconversion from the DART files was done. Increment the serial number here
  // serial_number++ and writing md5 filenames to detect and to avoid collisions.
  // For latter day reconversions the BENEFIT of maintaining the serial numbering was attractive.
  // If new raw DART material surfaces (perhaps from a 2nd reading of the original tapes)
  // then I would suggest generating yet higher serial numbers for the new lumps of content.

    size5 = wrdcnt*5; // Global var used in output_data7_file (sigh).

    serial_number = select_sn( data5_MD5_hash );
    assert( serial_number );
    assert( serial_number <= 999999 ); // Tolerate new ones

    if( snhash5[serial_number] )
      return 0; // Already did this one.
    snhash5[serial_number] = 1; // mark TRUE.
    return 1;
}

void
pdp10_file_statistics(){
  int i; char c;
  /* reset */
  pushj_count = 0;
  bit35_count = 0;
  crlf_count = 0;
  filetype = '?';
  nzbytes = 0;
  non_numeric_byte5_and_bit35 = 0;
  /* empty file content is type '?' */
  if(!data_word_count){
    return;
  }
  /*
   * Scan by 36-bit word, count specific OPCODE and TEXT patterns. 
   */
  for (i=0;i<data_word_count;i++){
    pdp10_word w;
    w.fw = obuf8[i];
    if ( w.instruction.op == 0260)
      pushj_count++;
    if ( w.seven.bit35 ){
      bit35_count++;
      c = w.seven.a5;
      // TVEDIT line numbers were marked with 8th bit in 5th digit.
      // TVEDIT pages were terminated with five spaces, 5th one as bits 0240.
      if (!( ('0'<=c && c<='9') || c==' '))
        non_numeric_byte5_and_bit35++;
    }
  }
  /*
   * Scan by 7-bit byte, count NON-Zero bytes and count CRLF pairs.
   */
  if ( obuf5[0] ) 
    nzbytes++;
  for ( i=1; i<size5; i++ ){
    if ( obuf5[i-1]=='\r' && obuf5[i]=='\n')
      crlf_count++ ;
    if ( obuf5[i] )
      nzbytes++;
  }
  /*
   * Determine type of data in file from content clues only.
   */
  filetype = '?';
  if ( pushj_count > 100 ){
    filetype = 'X';
  }
  /*
   * Test for text editor E header lines 
   */
  if ( !strncmp( (char *)obuf5, "COMMENT \26   VALID ",18) 
    && !strncmp( (char *)(obuf5+23), " PAGES\r\nC REC  PAGE   DESCRIPTION\r\n",34)){
    filetype = 'C';
  } else if ( crlf_count >= bit35_count/2 && non_numeric_byte5_and_bit35<=9 ) {
    /* CRLF new lines equals or exceeds half the bit35 count,
       with no significant number of binary looking byte5's.
       So ten words of noise tolerated inside a text file.
    */
    filetype = 'T';
  }
/*
 * Test for the appearance of the word 'copyright' 
 */
  copyright_hit = strcasestr((char *)obuf_utf8,"copyright") ? 'Y' : 'N';
  /*
   * Test for a video file header.
   */
  if (      obuf8[0]== 0xFFFFFFFFFLL 
       && ( obuf8[1]==4 || obuf8[1]==6 )
       && ((obuf8[7] & 0x800000000LL) != 0 )){
    filetype = 'V';
  }
}

void
process_data_record( int ma, char *dartfile ){
  int j, record_type, wrdcnt, record_length, mode, prot;
  uint64 check_sum_now, check_sum_word;
  char xorcheck[8];
  static int data_words_seen=0;
  /**/
  record_type = data8[ma].half.left;
  record_length = data8[ma].half.right;

  sixbit_word_into_ascii(  filnam, data8[ma+2] );
  sixbit_halfword_into_ascii( ext, data8[ma+3].half.left );
  sixbit_halfword_into_ascii( prj, data8[ma+5].half.left );
  sixbit_halfword_into_ascii( prg, data8[ma+5].half.right );

  waitsdate= data8[ma+3].word2.date_hi <<12 | data8[ma+4].word3.date_lo;
  waitstime= data8[ma+4].word3.time;
  waitsmode= data8[ma+4].word3.mode;
  waitsprot= data8[ma+4].word3.prot;
  sailprot= data8[ma+4].word3.prot; // sweeter by another name

  prot = waits2unixprot( waitsprot );
  mode = waitsmode;

  day = waitsdate % 31 + 1;
  month = (waitsdate/31) % 12 + 1;
  year = ((waitsdate/31) / 12 + 64) + 1900;
  hour = waitstime / 60;
  minute = waitstime % 60;
  second = 0;

  sprintf( iso_datetime, "%d-%02d-%02dT%02d:%02d:%02d", year,month,day, hour,minute,second );

  data_word_count = data8[ma+7].word6.count;
  assert( data_word_count < MAX_WORD_COUNT );
  // checksum check using XOR
  check_sum_now = 0;
  check_sum_word = (uint64)data8[ma+1+record_length].full.word;
  for( j=0; j<record_length; j++){
    check_sum_now ^= data8[ma+1+j].full.word;
  }
  if ( check_sum_now == check_sum_word ){
    sprintf(xorcheck,"^^OK^^");
    if (record_type == -3){
      data_words_seen = 0;
    }
    wrdcnt = record_length - 59;
    data_record_payload_to_output_buffers( ma+36, data_words_seen, wrdcnt );
    data_words_seen += wrdcnt;
    if ( data_words_seen == data_word_count ){
      int new_sn=0; // Boolean
      data8_MD5_hash[0] = 0; size8 = 0;
      data5_MD5_hash[0] = 0; size5 = 0;
      data7_MD5_hash[0] = 0; ubyte_cnt = 0;

#define TOKEN(x,y,z) ((x<<16)|(y<<8)|z)
  switch(TOKEN(prg[0],prg[1],prg[2])){
  case TOKEN(' ',' ','2'):
    // [2,2] email area is private.
    // [3,2] and [1,2] notices and announcements are public.
    Public = (!strncmp("  2",prj,3)) ? 'N' : 'Y';
    break;
  case TOKEN(' ',' ','3'):
  case TOKEN('B','G','B'):
  case TOKEN('D','O','C'):
  case TOKEN('C','S','R'):
  case TOKEN('S','Y','S'):
  case TOKEN('L','S','P'):
  case TOKEN('A','I','L'):
  case TOKEN('N','E','T'):
  case TOKEN('D','E','K'):
  case TOKEN('M','U','S'):
  case TOKEN('M','R','C'):
  case TOKEN('T','E','X'):
  case TOKEN('D','R','W'):
    Public = 'Y';
    break;
  default:
    Public = 'N';
    break;
  }
#undef TOKEN
      new_sn = output_data5_file( data_word_count );
      if ( new_sn ){
        pdp10_file_statistics();
        output_data7_file( data_word_count );
        // insert_text_into_database();
        output_data8_file( data_word_count );
        output_data13_file( data_word_count );
        fprintf(snhash,"%06d"   // 1. sn
                ",%s"           // 2. hash
                ",%s"           //    hash
                ",%s"           //    hash
                ",%c"           // 3. type
                ",%ld"          // 4. nzbytes
                ",%ld"          // 5. crlf
                ",%ld"          // 6. bit35
                ",%ld"          // 7. pushj
                ",%ld"          // 8. notdig
                ",%ld"          // 9. size8
                ",%d"           // 10. bytes
                ",%d"           // 11. chars
                ",%c" // string 'copyright' hit
                "\n",
                serial_number,                  // 1.
                data5_MD5_hash,                 // 2.
                data7_MD5_hash,
                data8_MD5_hash,
                filetype,                       // 3.
                nzbytes,                         // 4.
                crlf_count,                      // 5.
                bit35_count,                     // 6.
                pushj_count,                     // 7.
                non_numeric_byte5_and_bit35,     // 8.
                size8,                           // 9.
                ubyte_cnt,                       // 10.
                uchar_cnt,                       // 11.
                copyright_hit
                );
        fflush(snhash);        
      }

      {
        char dq_filnam[16];
        char dq_ext[16];
        char dq_prj[16];
        char dq_prg[16];
        char flat_unixname[48];
        dq( dq_filnam, filnam );
        dq( dq_ext, ext );
        dq( dq_prj, prj );
        dq( dq_prg, prg );
        unixname( flat_unixname, filnam, ext, prj, prg );
        fprintf( stdcsv,        // 1. id ( auto increment when loading empty field )
                 ",%c"          // 2. public
                 ",%d"          // 3. words
                 ",%s"          // 4. dt
                 ",%s"          // 5. filnam
                 ",%s"          // 6. ext
                 ",%s"          // 7. prj
                 ",%s"          // 8. prg
                 ",%03o"        // 9. prot
                 ",%03o"        // 10. mode
                 ",%ld"         // 11. size5
                 ",%s"          // 12. hash5
                 ",%s"          // 13. dartfile
                 ",%d"          // 14. tapeno
                 ",%s"          // 15. unixname
                 ",%03o"        // 16. sailprot         original PROT field
                 "\n",
                 Public,                          // 2.
                 data_word_count,                 // 3.
                 iso_datetime,                    // 4.
                 dq_filnam,                       // 5.
                 dq_ext,                          // 6.
                 dq_prj,                          // 7.
                 dq_prg,                          // 8.
                 prot,                            // 9.
                 mode,                            // 10.
                 size5,                           // 11.
                 data5_MD5_hash,                  // 12.
                 rindex(dartfile,'p'),            // 13.
                 tapeno,                          // 14.
                 flat_unixname,                   // 15.
                 sailprot                         // 16.
                 );
        fflush(stdcsv);
      }
    }
  } else {
    sprintf(xorcheck,"^FAIL^");
    sprintf(str,"%s.%s[%s,%s] %8d words %s",
          filnam,ext,prj,prg, data_word_count,
          xorcheck );
    fprintf(stderr,"checksum  now = %012llo\n", check_sum_now );
    fprintf(stderr,"checksum word = %012llo %s %s %d\n", check_sum_word,str,rindex(dartfile,'p'),tapeno);
    sprintf(xorcheck,"^FAIL^");
  }
  sprintf(str,"%s.%s[%s,%s] %8d words %s",
          filnam,ext,prj,prg, data_word_count,
          xorcheck );
}

/*
 * DART tape HEAD or TAIL record version #6 found.
 *
// fragment from DART.FAI 1990-06-05 11:10:00
// The HEADER AND TRAILER BLOCKS:
 Word  0: version,,length
                "version" is the positive version number of
                 the DART that created this tape (VERSION);
                "length" is the length of the data following,
                 including the rotated checksum word below.
 Word  1: 'DART  ' sixbit DART
 Word  2: '*HEAD*' or '*TAIL*'
                data in sixbit, indicates Header or Trailer block
 Word  3: time,date	in file system format
                Bits 13:23 are time in mins, bits 24:35 are date.
                Bits 4:12 are unused (considered high minutes).
                Version 5: Bits 0-2 are high date.
                Version 6: Bit 3 means this is time from prev media
 Word  4: ppn		the name of the person running Dart.
 Word  5: class,,tapno	Tape number of this tape
                Dump class of this dump
                The top bits of tapno indicate which
                sequence of tapes this nbr is in.
 Word  6: relative dump,,absolute dump	(relative is within this tape)
 Word  7: tape position in feet from load point
 Word  8:	 0		reserved for future use
 Word  9:	-1		all bits on
 Word 10:	 0		all bits off
 Word 11.	rotated checksum of words 1 through 12 above.
//
// FILE data records -3 start FILE, 0 continue FILE data.
//
        word  0. Type -3 or 0 ,, record_length_in_words = ( 36. + data_word_count + 23. )
        word  1. sixbit'DSK   '  == 0446353000000
00      word  2. DDNAM          sixbit/FILNAM/
01      word  3. DDEXT          sixbit/EXT/ ,, 
02      word  4. DDPRO
03      word  5. DDPPN          sixbit/PRJPRG/
04      word  6. DDLOC
05      word  7. DDLNG          data_word_count
06      word  8. DREFTM
07      word  9. DDMPTM
10      word 10. DGRP1R
11      word 11. DNXTGP
12      word 12. DSATID
13      word 13. DQINFO         special info block 5 words
14      word 14.
15      word 15.
16      word 16.
17      word 17.
        word 18. sixbit'DART  '  == 0444162640000
        word 19. sixbit'*FILE*' or sixbit'*CONT*'
        word 20.
        word 21.
        word 22.
        word 23.
        word 24.
        word 25.
        word 26.  -1
        word 27.
        word 28.
        word 29.
        word 30.
        word 31.
        word 32.
        word 33.
        word 34.
        word 35.
---------------------------------------------------------------------
        word 36.                        First word of the DATA
        word 36 + data_word_count - 1.  Final word of the DATA
---------------------------------------------------------------------
        23. more words, let us call them -23 to -1
        by observation -18 to -1 are always zero
---------------------------------------------------------------------
//
// fragment from DART.FAI 1990-06-05 11:10:00
//
 ; Add up rotated checksum for header or trailer, return checksum in A.
 ; Call with B pointing to first word, containing VERSION,,WD.CNT.
 ; We trust the WD.CNT as being valid, so we add up that many words, minus 1
 ; (we don't add in the last word, as it will become the rotated checksum).
        ROTCHK:	 MOVN A,(B)		;get negative wd count
                 HRLI B,1(A)		;make aobjn ptr, don't include the checksum word
                 TDZA A,A		;add checksum up here
        ROTCHL:	 ROT A,1		;rotate previously computed checksum
                 ADD A,1(B)		;add in new data
                 AOBJN B,ROTCHL		;loop through data
                 POPJ P,
*/

int
process_dart_head_or_tail(ma){
  int j;
  char prj[4],prg[4];
  int version, Length; // word 0: version,,length
  unsigned long long rotchk=0, word10, word11, word12, word13;
  //  unsigned long long word3, word4, word5;
  /**/
  bzero(str,sizeof(str));
  version = data8[ma].half.left;
  Length = data8[ma].half.right;
  sixbit_word_into_ascii(str, data8[ma+1] ); // DART
  sixbit_word_into_ascii(str+6, data8[ma+2] ); // *HEAD* or *TAIL*

  waitsdate= data8[ma+3].head_tail_word3.date_hi <<12 | data8[ma+3].head_tail_word3.date_lo;
  waitstime= data8[ma+3].head_tail_word3.time;

  day = waitsdate % 31 + 1;
  month = (waitsdate/31) % 12 + 1;
  year = ((waitsdate/31) / 12 + 64) + 1900;
  hour = waitstime / 60;
  minute = waitstime % 60;

  sprintf( iso_datetime, "%d-%02d-%02dT%02d:%02d", year,month,day, hour,minute );

  //  word3= (ulong)data8[ma+3].full.word;
  //  word4= (ulong)data8[ma+4].full.word;
  //  word5= (ulong)data8[ma+5].full.word;

  sixbit_halfword_into_ascii(prj, data8[ma+4].half.left );
  sixbit_halfword_into_ascii(prg, data8[ma+4].half.right );

  word10= (ulong)data8[ma+010].full.word;
  word11= (ulong)data8[ma+011].full.word;
  word12= (ulong)data8[ma+012].full.word;
  word13= (ulong)data8[ma+013].full.word;

  for (j=1;j<=012;j++){
    rotchk <<= 1;
    if ( 0x1000000000LL & rotchk ){
      rotchk |= 1;
      rotchk &= 0xFFFFFFFFFLL;
    }
    rotchk += (ulong)data8[ma+j].full.word;
    rotchk &= 0xFFFFFFFFFLL;
  }
  fprintf(stdlog,"%9s Tape Header record_type==6 %s %s "
          "dumped by [%s,%s] class=%d tape#%4d %o,,%o %4lo feet, version#%d length=%d",
          dartreel,
          str, iso_datetime, prj, prg,
          data8[ma+5].half.left, data8[ma+5].half.right,        // class,, tapeno
          data8[ma+6].half.left, data8[ma+6].half.right,        // 1,,1
          (ulong)data8[ma+7].full.word, // feet
          version, Length );
  if (strncmp("DART  *HEAD*",str,12)==0 && strncmp("DMP",prj,3)==0 && strncmp("SYS",prg,3)==0 ){
    tapeno = data8[ma+5].half.right;
  }
  if ( word10==0L && word11==0xFFFFFFFFFLL && word12==0 && word13==rotchk ){
    fprintf(stdlog,"rot-check OK\n");
  } else {
    fprintf(stdlog,"rot-check words %llo=?0, %llo=?-1, %llo=?0, %llo =? %llo FAILED.\n",
            word10,word11,word12,word13,rotchk);
  }
  fflush(stdlog);
  if (strncmp("DART  *TAIL*",str,12)==0 && strncmp(" MC",prj,3)==0)
    return 0;
  else
    return ma;
}

/* Main 
 * -------------------------------------------------------------------- *
 */
int
main(int ac,char **av){
  int ma, record_number, arg;
  {
    char *buf=0;
    int i=0;
    size_t len=16;
    FILE *F;
    assert(F = fopen("/d/2015/undart/sn.tmp","r"));
    while(getline(&buf,&len,F)>0){
      val[i++] = strtol(buf,0,5+5);
    }
    fclose(F);
    i=0;
    assert(F = fopen("/d/2015/undart/hash","r"));
    while(getline(&key[i++],&len,F)>0);
    fclose(F);
  }
  initialize_utf8_tables();
  /*
   * usage message if no arguments, output is optional 
   */
  if ( ac <= 1 ){
    fprintf(stderr,"\nusage: undart DART_FILE_NAMES\n");
    return 1;
  }
  /*
   * initialization 
   */
  errno=0;  stdlog = fopen("/d/2015/log/undart","w");  assert(errno==0);
  errno=0;  stdcsv = fopen("/d/2015/csv/undart","w");  assert(errno==0);
  errno=0;  snhash = fopen("/d/2015/csv/sn-hash","w"); assert(errno==0);

  /* main arguments are dart file names, first P3000.000 to last p3228.036 */
  for ( arg=1 ; arg<ac; arg++ ){
    strcpy( dartreel, av[arg] );
    bad_data_word = 0;
    ma_old = length_old = 0;
    convert_words_5bytes_to_8bytes( av[arg] );
    /*
     * Scan for DART tape records.
     */
    for( ma=0, record_number=1; ma < word_count; ma++, record_number++ ){
      /*
       * Two word RECORD prefix.
       */
      record_type = data8[ma].half.left;
      record_length = data8[ma].half.right;
      sixbit_word_into_ascii( device, data8[ma+1] ); 
      bzero(str,sizeof(str));
      /*
       * DETECT bad record header AND skip ahead if necessary.
       * Test for Unacceptable record TYPE or LENGTH or DEVICE marker. 
       */
      if ( !( record_type == -3 || record_type == 0 || record_type == 6 )
           || !(11 <= record_length && record_length < 10240)
           || !( strncmp("DSK   ",device,6)==0
                 || strncmp("DART  ",device,6)==0 
                 || strncmp(" ERROR",device,6)==0 )
           ){
        fprintf(stdlog,"%9s tape#%4d,BAD RECORD word#%8d record#%04d %6d,,%6d :%s:\n",
                dartreel, tapeno, ma, record_number, record_type, record_length, device );
        fflush(stdlog);
        record_type = -9; // FAKE record_type to UGLY
      }
      /*
       * Dispatch processing for the DART record types -3, 0 or 6.
       * ASSIGN record_type -9 for BAD header, but I may archive these byte spans as well,
       * there are 63 occurences of BAD header which become anonymous data blobs #01 to #63.
       */
      switch ( record_type ){
      case -9:
        {
          int ma_next, malo = ma;
          ma_next = advance_to_good_record_header( ma );
          record_length = ma_next - malo - 1;
        }
        break;
      case 0:
      case -3: 
        process_data_record( ma, av[arg] );
        ma++;
        break;
      case 6:
        process_dart_head_or_tail(ma);
        break;
      }
      fprintf(stdlog,"%9s tape#%4d, OK  word#%8d record#%04d %6d,,%6d :%s: %s\n",
              dartreel, tapeno,
              ma, record_number, record_type, 
              record_length, 
              device, str );
      fflush(stdlog);
      ma += record_length;
      assert( ma < MAX_WORD_COUNT );
    }
    if ( bad_data_word > 0 ){
      fprintf( stdlog, "%s skipped %12d data words\n", av[arg], bad_data_word );
      fflush(stdlog);
    }
  }
  fprintf(stderr,"final sn# is %6ld\n",snmax-1);
  return 0;
}
