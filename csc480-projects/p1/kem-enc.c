/* kem-enc.c
 * simple encryption utility providing CCA2 security.
 * based on the KEM/DEM hybrid model. */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <openssl/sha.h>

#include "ske.h"
#include "rsa.h"
#include "prf.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

static const char* usage =
"Usage: %s [OPTIONS]...\n"
"Encrypt or decrypt data.\n\n"
"   -i,--in     FILE   read input from FILE.\n"
"   -o,--out    FILE   write output to FILE.\n"
"   -k,--key    FILE   the key.\n"
"   -r,--rand   FILE   use FILE to seed RNG (defaults to /dev/urandom).\n"
"   -e,--enc           encrypt (this is the default action).\n"
"   -d,--dec           decrypt.\n"
"   -g,--gen    FILE   generate new key and write to FILE{,.pub}\n"
"   -b,--BITS   NBITS  length of new key (NOTE: this corresponds to the\n"
"                      RSA key; the symmetric key will always be 256 bits).\n"
"                      Defaults to %lu.\n"
"   --help             show this message and exit.\n";

#define FNLEN 255

enum modes {
    ENC,
    DEC,
    GEN
};

/* Let SK denote the symmetric key.  Then to format ciphertext, we
 * simply concatenate:
 * +------------+----------------+
 * | RSA-KEM(X) | SKE ciphertext |
 * +------------+----------------+
 * NOTE: reading such a file is only useful if you have the key,
 * and from the key you can infer the length of the RSA ciphertext.
 * We'll construct our KEM as KEM(X) := RSA(X)|H(X), and define the
 * key to be SK = KDF(X).  Naturally H and KDF need to be "orthogonal",
 * so we will use different hash functions:  H := SHA256, while
 * KDF := HMAC-SHA512, where the key to the hmac is defined in ske.c
 * (see KDF_KEY).
 * */

#define HASHLEN 32 /* for sha256 */

int kem_encrypt(const char* fnOut, const char* fnIn, RSA_KEY* K)
{
    /* TODO: encapsulate random symmetric key (SK) using RSA and SHA256;
     * encrypt fnIn with SK; concatenate encapsulation and cihpertext;
     * write to fnOut. */
    
    int leng = rsa_numBytesN(K);
    unsigned char* x = malloc(leng);
    unsigned char* encap = malloc(encapLen);
    unsigned char* h = malloc(HASHLEN);
    int fdout;
    SKE_KEY SK;
    
    randBytes(x, leng);
    ske_keyGen(SK, x, leng);
    size_t encapLen = leng + HASHLEN;
    if(leng != rsa_encrypt(encap, x, leng, K)) {return -1;}
    SHA256(x, leng, h);
    memcpy(encap+len, h, HASHLEN);
    fdout = open(fnOut, O_CREAT | O_RDWR, S_IRWXU);
    if(fdout == -1){ return -1;}
    write(fdout, encap, encapLen);
    ske_encrypt_file(fnOut, fnIn, &SK, NULL, encapLen);
    close(fdout);
    
    return 0;
}

int _encrypt(char* fnOut, char* fnIn, char* fnKey){
    FILE* keyFile = fopen(fnKey, "r");
    printf("File: %s\n", fnKey);
    if(keyFile == NULL){
        printf("FILE LOADING FAILED! \n");
        return -1;
    }
    RSA_KEY K;
    rsa_readPublic(keyFile, &K);
    kem_encrypt(fnOut, fnIn, &K);
    rsa_shredKey(&K);
    fclose(keyFile);
    
    return 0;
}
/* NOTE: make sure you check the decapsulation is valid before continuing */
int kem_decrypt(const char* fnOut, const char* fnIn, RSA_KEY* K)
{
    /* TODO: write this. */
    /* step 1: recover the symmetric key */
    /* step 2: check decapsulation */
    /* step 3: derive key from ephemKey and decrypt data. */
    unsigned char* encap = malloc(encapLen);
    unsigned char* x = malloc(rsaLen);
    unsigned char* h = malloc(HASHLEN);
    unsigned char* a = encap + rsaLen;
    
    size_t rsaLen = rsa_numBytesN(K);
    size_t encapLen = rsaLen + HASHLEN;
    size_t read = fread(encap, 1, encapLen, encrypted);
    FILE* encrypted = fopen(fnIn, "r");
    
    if(read != encapLen){return -1;}
    if(rsaLen != rsa_decrypt(x, encap, rsaLen, K)){ return -1;}
    SHA256(x, rsaLen, h);
    
    for(int i = 0; i < HASHLEN; i++){
        if(*h != *a){ return -1; }
        a++; h++;
    }
    
    SKE_KEY SK;
    ske_keyGen(&SK, x, rsaLen);
    ske_decrypt_file(fnOut, fnIn, &SK, encapLen);
    a = NULL;
    
    fclose(encrypted);
    return 0;
}

int main(int argc, char *argv[]) {
    /* define long options */
    static struct option long_opts[] = {
        {"in",      required_argument, 0, 'i'},
        {"out",     required_argument, 0, 'o'},
        {"key",     required_argument, 0, 'k'},
        {"rand",    required_argument, 0, 'r'},
        {"gen",     required_argument, 0, 'g'},
        {"bits",    required_argument, 0, 'b'},
        {"enc",     no_argument,       0, 'e'},
        {"dec",     no_argument,       0, 'd'},
        {"help",    no_argument,       0, 'h'},
        {0,0,0,0}
    };
    /* process options: */
    char c;
    int opt_index = 0;
    char fnRnd[FNLEN+1] = "/dev/urandom";
    fnRnd[FNLEN] = 0;
    char fnIn[FNLEN+1];
    char fnOut[FNLEN+1];
    char fnKey[FNLEN+1];
    memset(fnIn,0,FNLEN+1);
    memset(fnOut,0,FNLEN+1);
    memset(fnKey,0,FNLEN+1);
    int mode = ENC;
    // size_t nBits = 2048;
    size_t nBits = 1024;
    while ((c = getopt_long(argc, argv, "edhi:o:k:r:g:b:", long_opts, &opt_index)) != -1) {
        switch (c) {
            case 'h':
                printf(usage,argv[0],nBits);
                return 0;
            case 'i':
                strncpy(fnIn,optarg,FNLEN);
                break;
            case 'o':
                strncpy(fnOut,optarg,FNLEN);
                break;
            case 'k':
                strncpy(fnKey,optarg,FNLEN);
                break;
            case 'r':
                strncpy(fnRnd,optarg,FNLEN);
                break;
            case 'e':
                mode = ENC;
                break;
            case 'd':
                mode = DEC;
                break;
            case 'g':
                mode = GEN;
                strncpy(fnOut,optarg,FNLEN);
                break;
            case 'b':
                nBits = atol(optarg);
                break;
            case '?':
                printf(usage,argv[0],nBits);
                return 1;
        }
    }
    
    /* TODO: finish this off.  Be sure to erase sensitive data
     * like private keys when you're done with them (see the
     * rsa_shredKey function). */
    switch (mode) {
        case ENC:
        case DEC:
        case GEN:
        default:
            return 1;
    }
    
    return 0;
}
