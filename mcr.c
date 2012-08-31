#include "nbt.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#ifdef __WIN32__
#include <windows.h>
#include <winsock.h>
#else
#include <sys/mman.h>
#include <netinet/in.h>
#endif
#include <libgen.h>
#include <string.h>
#include <assert.h>

#define MCR_HEADER_SIZE 8192

// private structure
struct MCR {
    int fd;
    int readonly;
    uint32_t last_timestamp;
    struct MCRChunk {
        uint32_t timestamp;
        uint32_t len;
        unsigned char *data; // compression type + data
    } chunk[32][32];
};

int _mcr_read_chunk(MCR *mcr, int x, int z, void *header)
{
    assert(mcr && header);
    struct MCRChunk *chunk = &mcr->chunk[x][z];
    memset(chunk, 0, sizeof *chunk);
    
    // chunk location in header
    const unsigned char *b = (const unsigned char *)header + (4 * ((x % 32) + (z % 32) * 32));
    
    int offset = (b[0] << 16) | (b[1] << 8) | b[2];
    int nsect = b[3];
    
    // chunk not present, everything is 0
    if (offset == 0 && nsect == 0) return 0;
    
    // timestamp
    chunk->timestamp = ntohl(*(uint32_t*)b+4096);
    if (mcr->last_timestamp < chunk->timestamp) mcr->last_timestamp = chunk->timestamp;
    
    // read actual length
    lseek(mcr->fd, offset*4096, SEEK_SET);
    if (read(mcr->fd, &chunk->len, 4) != 4) return -1;
    chunk->len = ntohl(chunk->len);
    
    // read data
    chunk->len++; // it's weird, but some libs seem to forget one byte
    chunk->data = malloc(chunk->len);
    if (read(mcr->fd, chunk->data, chunk->len) < chunk->len-1) {
        free(chunk->data);
        chunk->data = NULL;
        return -1;
    }
    
    return 0;
}

void _mcr_free(MCR *mcr)
{
    if (mcr == NULL) return;
    for(int x=0; x < 32; x++) for(int z=0; z<32; z++)
        free(mcr->chunk[x][z].data);
    free(mcr);
}

struct MCR * mcr_open(const char *path, int mode)
{
    // check modes
    if (mode & O_APPEND) {
        errno = EINVAL;
        return NULL;
    }
    
    struct MCR *mcr = calloc(1, sizeof(struct MCR));
    if (mcr == NULL) return NULL;
    void *header = NULL;
    
    // open file
    #ifdef __WIN32__
    mode |= O_BINARY;
    #endif
    mcr->fd = open(path, mode, 0666);
    if (mcr->fd == -1) goto err;
    
    if (lseek(mcr->fd, 0, SEEK_END) == 0 && mode & O_CREAT && (mode & O_RDWR || mode & O_WRONLY)) {
        // new file
        mcr->last_timestamp = 1;
    } else {
        // read header
        if (mode == O_RDONLY) mcr->readonly = 1;
        header = malloc(MCR_HEADER_SIZE);
        lseek(mcr->fd, 0, SEEK_SET);
        if (read(mcr->fd, header, MCR_HEADER_SIZE) != MCR_HEADER_SIZE) goto err;

        // read chunks
        for(int x=0; x < 32; x++) for(int z=0; z<32; z++) {
            if (_mcr_read_chunk(mcr, x, z, header)) fprintf(stderr, "Error loading chunk %d,%d from %s\n", x,z,path);
        }
        
        free(header);
    }
    
    return mcr;
err:
    if (mcr->fd != -1) close(mcr->fd);
    free(header);
    _mcr_free(mcr);
    
    return NULL;
}

int mcr_close(MCR *mcr)
{
    assert(mcr);
    uint32_t *chunkLoc = NULL, *chunkTime = NULL;
    void *empty = NULL;
    
    if (!mcr->readonly) {
        // write file
        chunkLoc = calloc(1024, 4);
        chunkTime = calloc(1024, 4);
        empty = calloc(4096, 1);
        
        // write chunks
        lseek(mcr->fd, 8192, SEEK_SET);
        for(int x=0; x < 32; x++) for(int z=0; z<32; z++) {
            int i = x + z*32;
            struct MCRChunk *chunk = &mcr->chunk[x][z];
            if (chunk->data == NULL) continue;
            
            assert(lseek(mcr->fd, 0, SEEK_CUR)%4096 == 0);
            size_t chunkOffsetBlocks = lseek(mcr->fd, 0, SEEK_CUR) / 4096;
            size_t chunkLenBlocks = (chunk->len+4+4096) / 4096;
            if (chunkLenBlocks > 255) {
                errno = EFBIG;
                goto err;
            }
            chunkLoc[i] = (chunkOffsetBlocks << 8) | chunkLenBlocks;
            chunkTime[i] = chunk->timestamp;

            uint32_t chunkLen = htonl(chunk->len);
            if (write(mcr->fd, &chunkLen, 4) != 4) goto err;
            if (chunk->len != write(mcr->fd, chunk->data, chunk->len)) goto err;

            // write filling
            ssize_t fill = 4096-((chunk->len+4)%4096);
            if (write(mcr->fd, empty, fill) != fill) goto err;
            assert(lseek(mcr->fd, 0, SEEK_CUR) == (off_t)((chunkOffsetBlocks + chunkLenBlocks)*4096));
        }

        // write locations and timestamps
        lseek(mcr->fd, 0, SEEK_SET);
        for(int i=0; i<1024; i++) {
            chunkLoc[i] = htonl(chunkLoc[i]);
            chunkTime[i] = htonl(chunkTime[i]);
        }
        if (write(mcr->fd, chunkLoc, 4096) != 4096) goto err;
        if (write(mcr->fd, chunkTime, 4096) != 4096) goto err;

        free(chunkLoc); chunkLoc = NULL;
        free(chunkTime); chunkTime = NULL;
        free(empty); empty = NULL;
    }
    
    close(mcr->fd);
    _mcr_free(mcr);
    return 0;
    
err:
    close(mcr->fd);
    _mcr_free(mcr);
    free(chunkLoc);
    free(chunkTime);
    free(empty);
    return -1;
}

nbt_node *mcr_chunk_get(MCR *mcr, int x, int z)
{
    assert(mcr && x < 32 && z < 32 && x >= 0 && z >= 0);
    struct MCRChunk *chunk = &mcr->chunk[x][z];
    if (chunk->data == NULL) {
        errno = NBT_OK;
        return NULL;
    }
    return nbt_parse_compressed(chunk->data+1, chunk->len-1);
}

int mcr_chunk_set(MCR *mcr, int x, int z, nbt_node *root)
{
    assert(mcr && x < 32 && z < 32 && x >= 0 && z >= 0);
    if (mcr->readonly) {
        errno = EPERM;
        return -1;
    }
    struct MCRChunk *chunk = &mcr->chunk[x][z];
    if (root == NULL) {
        // delete chunk
        free(chunk->data);
        chunk->data = NULL;
        chunk->len = 0;
        chunk->timestamp = 0;
    } else {
        // compress chunk
        struct buffer compressed = nbt_dump_compressed(root, STRAT_INFLATE);
        chunk->len = compressed.len+1;
        uint8_t *data = malloc(chunk->len);
        if (data == NULL) {
            buffer_free(&compressed);
            return -1;
        }
        data[0] = 2; // compression type
        memcpy(data+1, compressed.data, compressed.len);
        buffer_free(&compressed);
        chunk->timestamp = mcr->last_timestamp;
        free(chunk->data);
        chunk->data = data;
    }
    
    return 0;
}
