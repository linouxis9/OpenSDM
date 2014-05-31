#include "main.h"

int main(int argc, char **argv)
{
    if(argc>1)
    {
        int result;
        FILE *in = fopen(argv[1],"r");
        FILE *out = fopen("wyjscie","w");
	
	//open key file
	void *keyFileName = malloc(strlen(argv[1]+5));
	sprintf((char*)keyFileName,"%s.key",argv[1]);
        FILE *key = fopen((char*)keyFileName,"r");
	if(key == NULL)
	  return errno;
	
	//load keyFileName
	fseek(key,0,SEEK_END);
	int unformattedLength = ftell(key);
	fseek(key,0,SEEK_SET);
	void *unformatted = malloc(unformattedLength);
	fread(unformatted,1,unformattedLength,key);
	
	//fill unpack structure
	UnpackData unpackData;
	unpackData.unformatted = unformatted;
	unformatted = NULL;
	char *keyStart = strstr((char*)unpackData.unformatted,"^^")+2;
	unpackData.fileNameKey = malloc(0x20);
	strncpy((char*)unpackData.fileNameKey,keyStart,0x20);
	unpackData.headerKey = malloc(0x20);
	strncpy((char*)unpackData.headerKey,keyStart+0x20,0x20);
	unpackData.checksum = strtoul((char*)unpackData.unformatted,NULL,10);
	unpackData.xorVal = strtoul(keyStart+0x40,NULL,10);

        //load header size
        uint8_t *hdrSizeBuff = (uint8_t*)malloc(4);
        fread(hdrSizeBuff,1,4,in);
        uint32_t headerSize = *(uint32_t*)hdrSizeBuff;

        //decode header
        CBlowFish *cbf1 = new CBlowFish();
        cbf1->Initialize((unsigned char*)unpackData.headerKey,32);
        Header *header = (Header*)malloc(headerSize);

        unsigned char *data = (unsigned char *)malloc(sizeof(Header));
        fread(data,1,headerSize,in);
        cbf1->Decode(data, (unsigned char *)header, headerSize);
//         free(data);
        data = NULL;

        //decode data from header
        CBlowFish *cbf2 = new CBlowFish();
        cbf2->Initialize((unsigned char*)unpackData.fileNameKey,32);
        data = (unsigned char *)malloc(sizeof(header->fileName));
        cbf2->Decode(header->fileName, data, sizeof(header->fileName));
        fprintf(stderr,"File name: %s\n",data);

        //ensure we are after header
        if(int r = fseek(in,headerSize+4,SEEK_SET)!=0)
            return r;

        //create inflate struct
        z_stream stream;// = (z_streamp)malloc(sizeof(z_stream));
        stream.next_in = Z_NULL;
        stream.avail_in = 0;
        stream.zalloc = Z_NULL;
        stream.zfree = Z_NULL;
        stream.opaque = Z_NULL;

        //initialize stream
        int r = inflateInit2_(&stream,-15,ZLIB_VERSION,(int)sizeof(z_stream));//NOTE: should be "1.2.2",0x38, or maybe not?
        if(r != Z_OK)
        {
            fprintf(stderr,"inflateInit2_ failed with errorcode %d (%s)\n",r,stream.msg);
            return r;
        }

        //read from file
        unsigned int bytesToRead = header->strangeSize & 0x3fff;
        unsigned char *input = (unsigned char*)malloc(bytesToRead);	//NOTE: probably a bit different number
        unsigned char *output = (unsigned char*)malloc(0x4000);	//exactly
        void *tmp = malloc(bytesToRead);
        while(true)
        {
            result = fread(input+stream.avail_in,1,bytesToRead-stream.avail_in,in);
	    if(result == 0)
	      return 0;
            fprintf(stderr,"sdc file read is at byte no. %ld (last fread returned %d)\n",ftell(in),result);

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
                fprintf(stderr,"inflate failed with errorcode %d (%s)\n",r,stream.msg);
                return r;
            }

            //XOR
            for(unsigned int i = 0; i < stream.total_out; i++)
            {
                output[i] ^= (unsigned char)unpackData.xorVal % 0x100;
            }

            //write to file
            fwrite(output,1,stream.total_out,out);

            /*
            * tricky part: input buffer hadn't been fully decompressed
            * so we need to copy the rest to TMP and then at hte beginning
            * of input buffer so it can be inflated, but before that we need to
            * read the rest of a chunk so its size would be STRANGESIZE
            */
            memcpy(tmp,stream.next_in,stream.avail_in);
            memcpy(input,tmp,stream.avail_in);
        }

        //write sdc header to &2
        uint8_t *headerBuff = (uint8_t*)header;
        for(int i = 0; i < 0x100; i++)
        {
            if(i%8==0)
                fprintf(stderr,"\n%04X:\t",i);
            fprintf(stderr,"0x%02X ",headerBuff[i]);
        }
        fprintf(stderr,"\n");
        fprintf(stderr,"crc32(0)=0x%lX\n",crc32(0,0,0));

	//TODO: free memory
//         delete cbf1;
//         delete cbf2;
// 	free(tmp);

        fclose(in);
        fclose(out);
    }
    return 0;
}

/*
 * Roadmap:
 * - convert to C
 * - do memory cleanup
 * - split into functions
 * - check CRC
 */