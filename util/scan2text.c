/*
    Converts Masscan "scan" binary format to text

    The "masscan/1.1" file format starts (and ends) with a 12 byte 
    string with the exact value of "masscan/1.1\0". If this string 
    isn't found, then the data is in some other format. We will 
    likely change this format in a couple months as we add new scan
    types (UDP, ICMP, etc.).
 
    The file consists of a series of TLV (type-length-value) records.
    The first field is the "type" of the record. The next field indicates
    the number of remaining bytes.

    Everything is BIG-ENDIAN.
 
    Both the the "type" and "length" fields are variable length. If the
    high-order bit is set, then the byte is followed by another one. Here are
    some examples
    0x05 - encodes the value '5'
    0x7F - encodes the value '127'
    0x80 0x00 - encodes the value '128'
    0x80 0x01 - encodes the value '129'
    0xFF 0x7F - encodes the value '32767'
    0x81 0x00 0x00 - encodes the value '32768'
    0x80 0x05 - encodes the value '5', unecessarily


    Some record types currently produced by the program:
    1 - STATUS (minimal open/closed port record)
    2 - BANNER (banner for a port)
    109 - FILEHEADER (the file header record)
 
    The FILEHEADER record is the first header in the file. It's contents are 
    always exactly 97 bytes long. That's because the file starts with the
    string "masscan", and the letter 'm' maps to the value 109, and the 
    letter 'a' maps to the value 97.
 

    The STATUS record is formatted as the following:

    +--------+
    |  0x01  |
    +--------+
    |  0x0C  |
    +--------+--------+--------+--------+
    |            timestamp              |
    +--------+--------+--------+--------+
    |          IPv4 address             |
    +--------+--------+--------+--------+
    |     TCP port    |
    +--------+--------+
    | reason |  either "rst" or "syn", ack or other flags too
    +--------+
    |  TTL   |
    +--------+
 
 The BANNER record is formatted as the following. Like all records,
 it starts with the type/length fields. I the length of the banner
 is too long, the length field may be more than 1 byte long. Like many
 other records, it contains a timestamp, IP address, and port number.
 The length of the banner-text is the length
 +--------+
 |  0x03  |
 +--------+ . . . .
 |? length:        :
 +--------+--------+--------+--------+
 |            timestamp              |
 +--------+--------+--------+--------+
 |          IPv4 address             |
 +--------+--------+--------+--------+
 |     TCP port    |
 +--------+--------+ . . . . . .  .  .  .   .   .   .    .    .     .
 | the banner text
 +--------+--------+ . . . . . .  .  .  .   .   .   .    .    .     .

*/
#define _CRT_SECURE_NO_WARNINGS
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

struct MasscanRecord {
    unsigned timestamp;
    unsigned ip;
    unsigned short port;
    unsigned char reason;
    unsigned char ttl;
};

static const size_t BUF_MAX = 1024*1024;

const char *
reason_string(unsigned x, char *buffer, size_t sizeof_buffer)
{
    sprintf(buffer, "%s%s%s%s%s%s%s%s",
        (x&0x01)?"fin-":"",
        (x&0x02)?"syn-":"",
        (x&0x04)?"rst-":"",
        (x&0x08)?"psh-":"",
        (x&0x10)?"ack-":"",
        (x&0x20)?"urg-":"",
        (x&0x40)?"ece-":"",
        (x&0x80)?"cwr-":""
        );
    if (buffer[0] == '\0')
        return "none";
    else
        buffer[strlen(buffer)-1] = '\0';
    return buffer;
}

void parse_status(const unsigned char *buf, size_t buf_length)
{
    struct MasscanRecord record;
    char timebuf[80];
    char addrbuf[20];
    char reasonbuf[80];

    if (buf_length < 12)
        return;
    
    /* parse record */        
    record.timestamp = buf[0]<<24 | buf[1]<<16 | buf[2]<<8 | buf[3];
    record.ip        = buf[4]<<24 | buf[5]<<16 | buf[6]<<8 | buf[7];
    record.port      = buf[8]<<8 | buf[9];
    record.reason    = buf[10];
    record.ttl       = buf[11];
    
    /* format time */
    {
        time_t timestamp = (time_t)record.timestamp;
        struct tm *tm;
        tm = localtime(&timestamp);
        
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm);
    }
    
    /* format IP into fixed-length field */
    sprintf(addrbuf, "%u.%u.%u.%u", 
            (record.ip>>24)&0xFF, (record.ip>>16)&0xFF,
            (record.ip>>8)&0xFF, (record.ip>>0)&0xFF);
    
    /* format reason (tcp flags) field */
    reason_string(record.reason, reasonbuf, sizeof(reasonbuf));
    
    
    /* output string */
    printf("%s %-15s :%5u %s %u\n",
           timebuf,
           addrbuf,
           record.port,
           reasonbuf,
           record.ttl);

}

/**
 * Normalize the string 'in-place' in the buffer. All non-printable characters,
 * include sensitive charactes like < and &, are converted to hex notation
 * like \x83.
 *
 * @param px
 *      the buffer containing the banner string we are normalizing
 * @param offset
 *      where within the buffer the banner starts
 * @param length
 *      where in the buffer the banner ends
 * @param max
 *      the maximum length of the buffer holding the banner, because as we
 *      increase the size of the banner string, we don't want to overwrite
 *      the end of the buffer.
 * @return
 *      a nul-terminated banner string with sensitive characters converted
 */
const char *
normalize_string(unsigned char *px, size_t offset, size_t length, size_t max)
{
    size_t i=0;
    px += offset;
    max -= offset;
    
    for (i=0; i<length; i++) {
        unsigned char c = px[i];
        
        if (isprint(c) && c != '<' && c != '>' && c != '&' && c != '\\') {
            /* do nothing */
        } else {
            if (i + 6 < max) {
                memmove(px+i+5, px+i, length-i+1);
                px[i++] = '\\';
                px[i++] = 'x';
                px[i++] = "0123456789abdef"[c >> 4];
                px[i  ] = "0123456789abdef"[c >> 0];
            }
        }
    }
    
    px[i] = '\0';
    
    return (char*)px;
}

/***************************************************************************
 ***************************************************************************/
const char *
banner_protocol_string(unsigned proto)
{
    switch (proto) {
    case 0: return "generic";
    case 1: return "SSHv1";
    case 2: return "SSHv2";
    case 3: return "HTTP";
    case 4: return "FTP";
    case 5: return "FTP";
    default: return "UNKNOWN";
    }
}

/***************************************************************************
 * Parse the BANNER record, extracting the timestamp, IP addres, and port
 * number. We also convert the banner string into a safer form.
 ***************************************************************************/
void parse_banner(unsigned char *buf, size_t buf_length)
{
    struct MasscanRecord record;
    unsigned proto;
    char timebuf[80];
    char addrbuf[20];
    
    /*
     * Parse the parts that are common to most records
     */
    record.timestamp = buf[0]<<24 | buf[1]<<16 | buf[2]<<8 | buf[3];
    record.ip        = buf[4]<<24 | buf[5]<<16 | buf[6]<<8 | buf[7];
    record.port      = buf[8]<<8 | buf[9];
    proto            = buf[10]<<8 | buf[11];
    
    /*
     * Pretty-print the timestamp format
     */
    {
        time_t timestamp = (time_t)record.timestamp;
        struct tm *tm;
        tm = localtime(&timestamp);
        
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm);
    }
    
    /* format IP into fixed-length field */
    sprintf(addrbuf, "%u.%u.%u.%u", 
            (record.ip>>24)&0xFF, (record.ip>>16)&0xFF,
            (record.ip>>8)&0xFF, (record.ip>>0)&0xFF);
    
    
    
    /* output string */
    if (buf_length > 12) {
        const char *s;
        s = normalize_string(buf, 12, buf_length-12, BUF_MAX);
        printf("%s %-15s :%5u %s \"%s\"\n",
               timebuf,
               addrbuf,
               record.port,
               banner_protocol_string(proto),
               s
               );
    }
}

/***************************************************************************
 ***************************************************************************/
char buf2[65536];
unsigned buf2len;
char buf3[65536];
unsigned buf3len;

/***************************************************************************
 * Read in the file, one record at a time.
 * @param filename
 *      The file we read.
 * @return
 *      the number of records successfully parsed
 ***************************************************************************/
uint64_t
parse_file(const char *filename)
{
    FILE *fp = 0;
    unsigned char *buf = 0;
    int bytes_read;
    uint64_t total_records = 0;
    
    buf = (unsigned char *)malloc(BUF_MAX);
    if (buf == 0) {
        fprintf(stderr, "memory allocation failure\n");
        goto end;
    }

    fp = fopen(filename, "rb");
    if (fp == NULL) {
        perror(filename);
        goto end;
    }

    /* first record is pseudo-record */
    bytes_read = fread(buf, 1, 'a'+2, fp);
    if (bytes_read < 'a'+2) {
        perror(filename);
        goto end;
    }

    /* Make sure it's got the format string */
    if (memcmp(buf, "masscan/1.1", 11) != 0) {
        fprintf(stderr, 
                "%s: unknown file format (expeced \"masscan/1.1\")\n", 
                filename);
        goto end;
    }

    /* Now read all records */
    for (;;) {
        unsigned type;
        unsigned length;

        buf3len = buf2len;
        memcpy(buf3, buf2, buf2len);
        buf2len = 0;

        /* [TYPE]
         * This is one or more bytes indicating the type of type of the
         * record
         */
        bytes_read = fread(buf, 1, 1, fp);
        if (bytes_read != 1)
            break;
        buf2[buf2len++] = buf[0];
        type = buf[0] & 0x7F;
        while (buf[0] & 0x80) {
            bytes_read = fread(buf, 1, 1, fp);
            if (bytes_read != 1)
                break;
            type = (type << 7) | (buf[0] & 0x7F);
            buf2[buf2len++] = buf[0];
        }
        
        /* [LENGTH]
         * Is one byte for lengths smaller than 127 bytes, or two
         * bytes for lengths up to 16384.
         */
        bytes_read = fread(buf, 1, 1, fp);
        if (bytes_read != 1)
            break;
        buf2[buf2len++] = buf[0];
        length = buf[0] & 0x7F;
        while (buf[0] & 0x80) {
            bytes_read = fread(buf, 1, 1, fp);
            if (bytes_read != 1)
                break;
            length = (length << 7) | (buf[0] & 0x7F);
            buf2[buf2len++] = buf[0];
        }
        if (length > BUF_MAX) {
            fprintf(stderr, "file corrupt\n");
            goto end;
        }
        
        
        /* get the remainder fo the record */
        bytes_read = fread(buf, 1, length, fp);
        if (bytes_read < length)
            break; /* eof */
        memcpy(buf2+buf2len, buf, length);
        buf2len += length;

        /* Depending on record type, do something different */
        switch (type) {
            case 1: /* STATUS: open */
            case 2: /* STATUS: closed */
                parse_status(buf, bytes_read);
                break;
            case 3: /* BANNER */
                parse_banner(buf, bytes_read);
                break;
            case 'm': /* FILEHEADER */
                //goto end;
                break;
            default:
                fprintf(stderr, "file corrupt: unknown type %u\n", type);
                goto end;
        }
        total_records++;
    }

end:
    if (buf)
        free(buf);
    if (fp)
        fclose(fp);
    return total_records;
}

/***************************************************************************
 ***************************************************************************/
int
main(int argc, char *argv[])
{
    int i;
    uint64_t total_records = 0;

    if (argc <= 1) {
        printf("usage:\n masscan2text <scanfile>\ndecodes and prints text\n");
        return 1;
    }
    fprintf(stderr, "--- scan2text for masscan/1.1 format ---\n");
    for (i=1; i<argc; i++) {
        total_records += parse_file(argv[i]);
    }
    fprintf(stderr, "--- %llu records scanned  ---\n", total_records);

    return 0;
}