#include "main.h"

int main(int argc, char **argv)
{
    //TODO: get rid of mem leaks (valgrind)
    uint8_t flags = 0;
    const char *sdcFile = NULL;
    int option;
    while((option = getopt_long(argc, argv, "fvVh", options, 0)) != -1)
    {
        switch(option)
        {
        case '?':
            return EXIT_INVALIDOPT;
            //force
        case 'f':
            flags |= F_FORCE;
            break;
            //verbose
        case 'v':
            flags |= F_VERBOSE;
            break;
            //version
        case 'V':
            print_version();
            return EXIT_SUCCESS;
            //help
        case 'h':
            print_help(PH_LONG,argv[0]);
            return EXIT_SUCCESS;
            break;
        default:
            print_help(PH_SHORT,argv[0]);
            return EXIT_INVALIDOPT;
        }
    }
    if((argc - optind) == 1)
    {
        //parsing argv successful
        sdcFile = argv[optind];
    }
    else
    {
        print_help(PH_SHORT,argv[0]);
        return EXIT_TOOLESS;
    }

    //TODO: rewrite msg to state first
    printf("Opening SDC file...\t\t");
    int result;
    FILE *in = fopen(sdcFile,"r");
    if(in == NULL)
    {
        //error opening a file
        printf("[FAIL]\n");
        perror(sdcFile);
        return errno;
    }
    printf("[OK]\n");

    //open key file
    void *keyFileName = malloc(strlen(sdcFile)+5);
    sprintf((char*)keyFileName,"%s.key",sdcFile);
    FILE *key = fopen((char*)keyFileName,"r");
    if(key == NULL)
    {
        //error opening a file
        printf("[FAIL]\n");
        perror((char*)keyFileName);
        return errno;
    }

    printf("Verifying keyfile...\t\t");

    //load keyFileName
    fseek(key,0,SEEK_END);
    int unformattedLength = ftell(key);
    fseek(key,0,SEEK_SET);
    void *unformatted = malloc(unformattedLength+1);
    fread(unformatted,1,unformattedLength,key);
    ((unsigned char *)unformatted)[unformattedLength] = '\0';
    fclose(key);

    //fill unpack structure
    UnpackData unpackData;
    UnpackStatus us = fillUnpackStruct(&unpackData,unformatted);
    switch(us)
    {
    case FUS_OK:
        printf("[OK]\n");
        break;
    default:
        printf("[FAIL]\n");
        fprintf(stderr, "%s: Wrong format of a keyfile!\n", argv[0]);
        return us;
    }

    //load header size
    uint8_t *hdrSizeBuff = (uint8_t*)malloc(4);
    fread(hdrSizeBuff,1,4,in);
    uint32_t headerSize = *(uint32_t*)hdrSizeBuff;
    free(hdrSizeBuff);
    hdrSizeBuff = NULL;

    printf("Validating SDC header...\t");

    //check header length
    if(headerSize < 0xff)
    {
        //it is not length but signature!
      printf("[FAIL]");
      fprintf(stderr,
              "%s: Encountered unsupported format! Signature is probably "
              "0x%02x\n", argv[0], headerSize);
      return -1;
    }

    //load and decode header
    Header *header = (Header*)malloc(headerSize);
    DecrError err = loadHeader(in, header, headerSize, &unpackData);
    if(err != DD_OK)
    {
      printf("[FAIL]");
      fprintf(stderr, "%s: Error when decrypting SDC header (errorcode: %d)\n", argv[0], err);
      return err;
    }

    //check if valid sdc file
    fseeko(in,0,SEEK_END);
    off_t sdcSize = ftello(in);//FIXME: check if still needed
    if((sizeof(Header) + (sizeof(File) * header->headerSize)) > headerSize)
    {
        printf("[FAIL]\n");
        fprintf(stderr, "%s: File given is not valid SDC file or decryption key wrong\n", argv[0]);
        if(! (flags & F_FORCE))
            return -1;
    }

    printf("[OK]\n");

    //check if number of files might be more than one
    if(header->headerSize > 1)
    {
        fprintf(stderr,
                "Warning! This container contains more than one file. However "
                "this program can only extract first one. Check for updates!\n"
               );
    }

    printf("Checking file integrity...\t");

    //count crc32
    uLong crc = countCrc(in, headerSize);
    if(flags & F_VERBOSE)
        fprintf(stderr, "%s: crc32: 0x%08lX; orig: 0x%08X\n", argv[0], crc, unpackData.checksum);

    //check if crc is valid
    if(crc != unpackData.checksum)
    {
        printf("[FAIL]\n");
        fprintf(
            stderr, "%s: CRC32 of sdc file did not match the one supplied in keyfile (0x%04X expected while have 0x%04lX)\n",
            argv[0], unpackData.checksum, crc
        );
        if(! (flags & F_FORCE))
            return crc;
    }
    else
        printf("[OK]\n");

    FileUnion *current = header->files;
    File *after = &header->files[header->headerSize].file;
    FileName *fn = (FileName*)after;

    printf("Decoding file name...\t\t");

    //decode data from header
    uint32_t fnLength = fn->fileNameLength;
    unsigned char *data = (unsigned char*)malloc(getDataOutputSize(fn->fileNameLength) + 1);
    err = decryptData(&fn->fileName, &fnLength, data, unpackData.fileNameKey, 32);
    if(err != DD_OK)
    {
      printf("[FAIL]");
      fprintf(stderr, "%s: Error while decrypting file name (errorcode: %d)", argv[0], err);
      return err;
    }
    memcpy((void*)&fn->fileName,data, fnLength);

    printf("[OK]\n");

    char *first_fn = &fn->fileName;
    uint32_t fn_size = fnLength;
    if(header->headerSize > 1)
    {
        //TODO: allow unpacking second and next files
        FileUnion *second = &header->files[1];
        fn_size = second->file.fileNameOffset;
        first_fn = (char*)malloc(fn_size);
        bzero(first_fn, fn_size);
        memcpy(first_fn, data, fn_size - 1);
    }

    if(flags & F_VERBOSE)
        fprintf(stderr,"File path: %s\n",first_fn);

    printf("Creating directory structure...\t");

    dosPathToUnix(first_fn);

    void *dirName = malloc(fn_size);
    strncpy((char*)dirName,first_fn,fn_size - 1);
    dirName = dirname((char*)dirName);

    char *baseName = basename(first_fn);

    //get sdc location
    char *sdcDir = (char*)malloc(strlen(sdcFile)+1);
    strcpy(sdcDir,sdcFile);
    sdcDir = dirname(sdcDir);

    //create directory according to header
    char *outFile = (char*)malloc(strlen(sdcDir)+strlen((char*)dirName)+2);
    sprintf(outFile,"%s/%s",sdcDir,(char*)dirName);
    DIR *f = NULL;
    if((f = opendir(outFile)) == NULL)
    {
        if(mkdir(outFile,S_IRWXU | S_IRWXG | S_IROTH | S_IWOTH | S_IXOTH) != 0)
        {
            //mkdir failed
            fprintf(stderr,"%s: Directory '%s' creation failed with errno: %d\n",argv[0], outFile,errno);
            return errno;
        }
    }
    else
        closedir(f);
    f = NULL;

    printf("[OK]\n");

    if(flags & F_VERBOSE)
    {
#define TIMESIZE	20
      char crtime[TIMESIZE];
      time_t creation = winTimeToUnix(current->file.creationTime);
      unixTimeToStr(crtime, TIMESIZE, creation);

      char actime[TIMESIZE];
      time_t access = winTimeToUnix(current->file.accessTime);
      unixTimeToStr(actime, TIMESIZE, access);

      char mdtime[TIMESIZE];
      time_t modification = winTimeToUnix(current->file.modificationTime);
      unixTimeToStr(mdtime, TIMESIZE, modification);

      fprintf(stderr, "File has been originally created at %s, last accessed at %s and modified at %s\n", crtime, actime, mdtime);
    }

    printf("Unpacking file(s)...\t\t");

    //open output file
    outFile = (char*)realloc(outFile, strlen(sdcDir)+strlen((char*)dirName)+strlen(baseName)+3);
    sprintf(outFile,"%s/%s/%s",sdcDir,(char*)dirName,baseName);
    FILE *out = fopen(outFile,"w");
    if(out == NULL)
    {
        //error opening a file
        printf("[FAIL]\n");
        perror(outFile);
        return errno;
    }

    //memory cleanup
    free(outFile);
    outFile = NULL;
    //free(sdcDir);//FIXME: SIGABRT
    sdcDir = NULL;
    free(dirName);
    dirName = NULL;

    //ensure we are after header
    int r;
    if((r = fseek(in,headerSize+4,SEEK_SET))!=0)
        return r;

    //create inflate struct
    z_stream stream;
    stream.next_in = Z_NULL;
    stream.avail_in = 0;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;

    //initialize stream
    r = (int)-1;
    if(header->headerSignature == SIG_ELARGE)
        r = inflateInit(&stream);
    else
        r = inflateInit2_(&stream,-15,ZLIB_VERSION,(int)sizeof(z_stream));
    if(r != Z_OK)
    {
        printf("[FAIL]\n");
        fprintf(stderr,"inflateInit failed with errorcode %d (%s)\n",r,stream.msg);
        return r;
    }
    //read from file
    unsigned int bytesToRead;
    if(header->headerSignature == SIG_ELARGE)
    {
        bytesToRead = current->file4gb.compressedSize & 0x3fff;
    }
    else
    {
        bytesToRead = current->file.compressedSize & 0x3fff;
    }
    unsigned char *input = (unsigned char*)malloc(bytesToRead);
    unsigned char *output = (unsigned char*)malloc(0x4000);
    void *tmp = malloc(bytesToRead);

    //determine file size
    unsigned int bytesRemaining = 0;
    if(header->headerSignature == SIG_ELARGE)
        bytesRemaining = current->file4gb.fileSize;
    else
        bytesRemaining = current->file.fileSize;

    if(flags & F_VERBOSE)
        fprintf(stderr,"file size has been set as %u (0x%04X), signature: 0x%02X\n",bytesRemaining,bytesRemaining,header->headerSignature);

    while(bytesRemaining != 0)
    {
        result = fread(input+stream.avail_in,1,bytesToRead-stream.avail_in,in);
        if(result == 0 && stream.avail_in == 0)	//stop only if stream iflated whole previous buffer
            return 1;				//still have bytes remaining but container end reached

        //decode
        stream.next_in = (Bytef*)input;
        stream.avail_in += result;
        stream.next_out = (Bytef*)output;
        stream.avail_out = 0x4000;
        stream.total_in = 0;
        stream.total_out = 0;
        r = inflate(&stream,0);
        if(r < Z_OK)
        {
            printf("[FAIL]\n");
            fprintf(stderr,"inflate failed with errorcode %d (%s)\n",r,stream.msg);
            return r;
        }

        //XOR
        xorBuffer(unpackData.xorVal % 0x100, output, stream.total_out);

        //write to file
        fwrite(output,1,stream.total_out,out);
        bytesRemaining -= stream.total_out;

        /*
        * tricky part: input buffer hadn't been fully decompressed
        * so we need to copy the rest to TMP and then at the beginning
        * of input buffer so it can be inflated, but before that we need to
        * read the rest of a chunk so its size would be COMPRESSEDSIZE
        */
        memcpy(tmp,stream.next_in,stream.avail_in);
        memcpy(input,tmp,stream.avail_in);
    }

    if(bytesRemaining != 0)
    {
        printf("[FAIL]\n");
        fprintf(stderr, "%s: Unexpected end of file!\n", argv[0]);
    }
    else
        printf("[OK]\n");

    free(tmp);
    tmp = NULL;
    free(input);
    input = NULL;
    free(output);
    output = NULL;
    free(unpackData.unformatted);
    unpackData.unformatted = NULL;
    unpackData.fileNameKey = NULL;
    unpackData.headerKey = NULL;

    //write sdc header to &2
    uint8_t *headerBuff = (uint8_t*)header;
    if(flags & F_VERBOSE)
    {
        unsigned int i;
        for(i = 0; i < 0x200; i++)
        {
            if(i%8==0)
                fprintf(stderr,"\n%04X:\t",i);
            fprintf(stderr,"0x%02X ",headerBuff[i]);
        }
        fprintf(stderr,"\n");
    }

    free(header);

    fclose(in);
    fclose(out);
    return 0;
}
