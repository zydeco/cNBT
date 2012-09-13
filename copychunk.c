#include "nbt.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#define  say(...)  printf("[CopyChunk] ");printf(__VA_ARGS__);fflush(stdout);
#define  err(...)  fprintf(stderr,"[CopyChunk] <ERROR> ");fprintf(stderr,__VA_ARGS__);exit(1);
#define  VERSION "0.4"

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

void parse_args_coords(int *x1, int *y1, int *x2, int *y2, int *rx1, int *ry1, int *rx2, int *ry2, char **argv);
void parse_args_world(char **world_name,char **orig);
int coord_from_char(char* input);
int coord_normalize(int r, int l,int def);
void region_check(char *region_filename,char *world);
MCR* region_open(char *region_filename,char *world,int mode);

void usage(void) {
    fprintf(stderr, "\nUsage: copychunk [src] [target] [x1] [y1] [x2] [y2]");
    fprintf(stderr, "\n     [src] and [target] are both paths to minecraft world directories");
    fprintf(stderr, "\n     [x1] [y1] [x2] [y2] are chunk coords (not block coords) of a square region\n\n");
    exit(1);
}

int main(int argc, char** argv) {
    // X,Y for chunks, regions, and internal chunks, respectively
    int x1, y1, x2, y2, rx1, ry1, rx2, ry2, ix1, iy1, ix2, iy2 = 0;
    char* source_world;
    char* target_world;
    char region_filename[256];

    // Check number of arguments
    say("Version %s\n",VERSION);
    if(argc < 7 || argc > 7) { usage(); }

    // Check coordinate arguments
    parse_args_coords(&x1,&y1,&x2,&y2,&rx1,&ry1,&rx2,&ry2,argv);

    // Check the world folders exist
    parse_args_world(&source_world,&argv[1]);
    parse_args_world(&target_world,&argv[2]);

    // Make sure all region files exist before beginning
    for(int rx = rx1; rx <= rx2; rx++) {
        for(int ry = ry1; ry <= ry2; ry++) {
            sprintf(region_filename,"r.%d.%d.mca",rx,ry);
            region_check(region_filename,source_world);
            region_check(region_filename,target_world);
        }
    }

    for(int rx = rx1; rx <= rx2; rx++) {
        for(int ry = ry1; ry <= ry2; ry++) {
            sprintf(region_filename,"r.%d.%d.mca",rx,ry);

            // Normalize chunk locations relative to region 
            ix1 = coord_normalize(rx,x1,0);
            iy1 = coord_normalize(ry,y1,0);
            ix2 = coord_normalize(rx,x2,31);
            iy2 = coord_normalize(ry,y2,31);
            
            // Open region files
            MCR *source = region_open(region_filename,source_world,O_RDONLY);
            MCR *target = region_open(region_filename,target_world,O_RDWR);

            say("Copying chunks (%d,%d) through (%d,%d) ... ",ix1,iy1,ix2,iy2);
            // Walk chunks
            int count = 0;
            for(int x = ix1; x <= ix2; x++) {
                for(int y = iy1; y <= iy2; y++) {
                    count++;
                    uint32_t timestamp = target->chunk[x][y].timestamp;
                    mcr_chunk_set(target,x,y,mcr_chunk_get(source,x,y));
                    target->chunk[x][y].timestamp = timestamp;
                }
            }
            printf("%d chunks copied\n",count); 
            mcr_close(source);

            say("Writing target region...\n");
            mcr_close(target);
        }
    }

    say("All Done!\n");
    return 0;
}

// Parse all coordinate arguments and check for validity
void parse_args_coords(int *x1, int *y1, int *x2, int *y2, int *rx1, int *ry1, int *rx2, int *ry2, char **argv) {
    // Read chunk coordinates
    *x1 = coord_from_char(argv[3]);
    *y1 = coord_from_char(argv[4]);
    *x2 = coord_from_char(argv[5]);
    *y2 = coord_from_char(argv[6]);
    say("Chunk coordinates: %d,%d to %d,%d\n",*x1,*y1,*x2,*y2);

    if (*x2<*x1 || *y2<*y1) {
        err("Coordinates not in order.  Please provide numerically lower corner first.\n");
    }

    // Check region shape
    int w = (*x2-*x1)+1;
    int h = (*y2-*y1)+1;
    if (w != h) {
        err("Area specified is not square (%d by %d), aborting.\n",w,h);
    }
    say("%d chunks (%d by %d)\n",w*h,w,h);

    // Determine region coords
    *rx1 = *x1 >> 5;
    *ry1 = *y1 >> 5;
    *rx2 = *x2 >> 5;
    *ry2 = *y2 >> 5;
    int rh = (*rx2-*rx1)+1;
    int rw = (*ry2-*ry1)+1;
    say("%d region file(s): %d,%d to %d,%d\n",rh*rw,*rx1,*ry1,*rx2,*ry2);
}

// Verify world folder exists
void parse_args_world(char **world_name,char **orig) {
    struct stat st;

    *world_name = malloc(strlen(*orig)+1);
    strcpy(*world_name,*orig);

    if(stat(*world_name,&st) != 0) {
        err("Cannot open world folder: %s\n",*world_name);
    }
}

// Convert a string coordinate arg to integer "safely"
int coord_from_char(char *input) {
    char *end;
    int ret_val = strtol(input,&end,10);
    if (*end) {
        err("Error converting (%s) to integer.\n",input);
    }
    return ret_val;
}

// Normalize a chunk coord to its location within a region
// If chunk lies outside the region, return "def" as a min/max bound
int coord_normalize(int r,int l,int def) {
    int tmp;
    if( ((r << 5) <= l) && (((r+1) << 5) > l)) {
        tmp = l % 32;
        if (tmp < 0) {
            return tmp+32;
        } else {
            return tmp;
        }
    } else {
        return def;
    }
}

void region_check(char *region_filename,char *world) {
    struct stat st;
    char *path = malloc(strlen(region_filename) + strlen(world) + 9);
    sprintf(path,"%s/region/%s",world,region_filename);
    if(stat(path,&st) != 0) {
        err("A required region file does not exist: %s\n[!] All region files must exist in both worlds for successful copy.\n[!] Aborting.\n",path);
    }
}

// Open a region file for work
MCR *region_open(char *region_filename,char *world,int mode) {
    char *path = malloc(strlen(region_filename) + strlen(world) + 9);
    sprintf(path,"%s/region/%s",world,region_filename);
    MCR *ret_region = mcr_open(path,mode);
    if (mode == O_RDONLY) {
        say("Opened for reading: %s\n",path);
    } else {
        say("Opened for writing: %s\n",path);
    }
    if(ret_region == NULL) {
        err("Unknown error opening region file: %s\n",path);
    }
    free(path);
    return ret_region;
}


