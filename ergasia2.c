#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#define FRAME_SIZE 4096
#define LOGICAL_ADDRESS_BITS 32
#define OFFSET_BITS Logarithm(FRAME_SIZE, 2)  /* Offset must be able to specify each byte of a frame */
#define PAGE_NUM_BITS (LOGICAL_ADDRESS_BITS - OFFSET_BITS)  /* The rest of a logical address (besides offset) is the page number */
#define BUFFER_SIZE Bit_Size_To_Hex_Size(LOGICAL_ADDRESS_BITS) + 2  /* The logical address is followed by a space and a character (given reference format) */
#define NUM_OF_FILES 2
#define OK 0
#define ERROR !OK
#define INVALID -1
#define FALSE 0
#define TRUE !FALSE

#define GIVE_INSTRUCTIONS_AND_STOP {  /* In case of invalid input */  \
    printf("To execute using LRU algorithm:\n./ergasia2 LRU <num_of_frames> <q> <max_num_of_references>\n\n");  \
    printf("To execute using WS algorithm:\n./ergasia2 WS <num_of_frames> <q> <ws_size> <max_num_of_references>\n\n");  \
    printf("NOTE: It is optional to provide <max_num_of_references>\n");  \
    return ERROR;  \
}

typedef struct Frame_Type {
    char data[FRAME_SIZE];  /* Frame consists of a defined number of bytes */
} Frame;

typedef enum Process_ID {  /* ID to uniquely identify each process */
    BZIP,
    GCC
} Process_ID;

typedef struct IPT_Entry_Type {  /* Each entry of the Inverted Page Table corresponds to a frame of main memory */
    Process_ID pid;  /* The ID of the process that currently uses this entry */
    int page_num;  /* The identifier of the hosted page */
    int timestamp;  /* Indicates the last time (virtual, not actual time) there was a reference to the hosted page */
    bool modified;  /* Shows whether the hosted page has been written since the last time it got loaded from hard disk */
    bool valid;  /* If this is true, the rest information of the entry is reliable. Else it is trash and the entry is actually empty */
} IPT_Entry;

typedef struct Reference_Type {  /* Request to perform an action to a specific data of a page */
    int page_num;  /* The identifier of the page */
    int offset;  /* Specify in which point of the page the desired data begins */
    char action;  /* 'R' stands for READ and 'W' stands for WRITE (Anything else is invalid and will lead to error) */
} Reference;

double Logarithm(double x, int base) {  /* Calculate log(x) with given base */
    return log(x) / log(base);  /* Change of base formula */
}

int Bit_Size_To_Hex_Size(int num_of_bits) {  /* Find the minimum number of hex digits to represent the same numeric values as the given number of bits */
    int hex_base = 16;
    int bits_per_hex_digit = Logarithm(hex_base, 2);  /* Each hex digit can be broken down to 4 bits */
    int num_of_hex_digits = ceil(num_of_bits / bits_per_hex_digit);  /* Round up if the number of bits is not multiple of 4 */
    return num_of_hex_digits;
}

void Print_Not_Null_Terminated_String(char *str, int length) {  /* Used to print not null-terminated strings (relies on given length) */
    for (int i = 0; i < length; i++) {
        printf("%c", str[i]);  /* Print each character */
    }
    printf("\n");
}

Reference TranslateBuffer(char buffer[BUFFER_SIZE]) {  /* Extract information from the contents of the buffer */	
    Reference reference;  /* A reference consists of page number, offset and action */
    int page_num_hex_size = Bit_Size_To_Hex_Size(PAGE_NUM_BITS);  /* Convert to hex size, because we have the ASCII of hex digits */
    char page_num_str[page_num_hex_size + 1];  /* It fits the hex representation of page number and '\0' */
    memcpy(page_num_str, buffer, page_num_hex_size);  /* Copy the hex representation of page number from the buffer */
    page_num_str[page_num_hex_size] = '\0';  /* Add '\0' at the end so it is an actual string */
    reference.page_num = strtol(page_num_str, NULL, 16);  /* Convert string in hex to integer */
    int offset_hex_size = Bit_Size_To_Hex_Size(OFFSET_BITS);  /* Convert to hex size, because we have the ASCII of hex digits */
    char offset_str[offset_hex_size + 1];  /* It fits the hex representation of offset and '\0' */
    memcpy(offset_str, (buffer + page_num_hex_size), offset_hex_size);  /* Copy the hex representation of offset from the buffer */
    offset_str[offset_hex_size] = '\0';  /* Add '\0' at the end so it is an actual string */
    reference.offset = strtol(offset_str, NULL, 16);  /* Convert string in hex to integer */
    reference.action = buffer[BUFFER_SIZE - 1];  /* The last character of the buffer specifies the action (READ or WRITE) */
    return reference;
}

void WS_Insert_Page(int *working_set, int ws_size, int page_num) {  /* Insert page to working set */
    for (int i = 1; i < ws_size; i++) {
        working_set[i - 1] = working_set[i];  /* Do the necessary shift to the already existing pages */
    }
    working_set[ws_size - 1] = page_num;  /* Add the newcomer to the end of the working set */
}

void WS_Remove_Page(int *working_set, int ws_size, int page_num) {  /* Remove page from working set */
    for (int i = 0; i < ws_size; i++) {
        if (working_set[i] == page_num) {  /* If the specified page is found */
            working_set[i] = INVALID;  /* Release its slot */
            break;
        }
    }
}

bool WS_Includes_This_Page(int *working_set, int ws_size, int page_num) {  /* Check whether the working set includes this page or not */
    for (int i = 0; i < ws_size; i++) {
        if (working_set[i] == page_num)
            return TRUE;  /* The specified page is found */
    }
    return FALSE;
}

int main(int argc, char *argv[]) {
    if (argc < 4 || argc > 6 || (argc > 1 && strcmp(argv[1], "LRU") != 0 && strcmp(argv[1], "WS") != 0))  /* Invalid number of arguments or invalid algorithm */
        GIVE_INSTRUCTIONS_AND_STOP;
    
    char algorithm[4];  /* Enough characters to save LRU and '\0' (or WS and '\0') */
    strcpy(algorithm, argv[1]);  /* The algorithm's name as a string */
    int num_of_frames = atoi(argv[2]);  /* The number of available frames in main memory */
    int q = atoi(argv[3]);  /* After q resolved references in one file continue to the other one */
    
    printf("\nSpecifications:\n");
    printf("Algorithm: %s\n", algorithm);
    printf("Number of frames: %d\n", num_of_frames);
    printf("Number q: %d\n", q);
    
    int ws_size = INVALID;  /* This determines how many pages each working set can carry simultaneously */
    int max_num_of_references = INVALID;  /* After resolving this number of references (in total) the simulation ends */
    if (strcmp(algorithm, "LRU") == 0) {
        if (argc == 5) {  /* User provided max_num_of_references (it is optional) */
            max_num_of_references = atoi(argv[4]);
            printf("Max number of references: %d\n", max_num_of_references);
        }
        else if (argc == 6)  /* Too many arguments for LRU */
            GIVE_INSTRUCTIONS_AND_STOP;
    }
    else if (strcmp(algorithm, "WS") == 0) {
        if (argc < 5)  /* Too few arguments for WS */
            GIVE_INSTRUCTIONS_AND_STOP;
        ws_size = atoi(argv[4]);
        printf("Working set size: %d\n", ws_size);
        if (argc == 6) {  /* User provided max_num_of_references (it is optional) */
            max_num_of_references = atoi(argv[5]);
            printf("Max number of references: %d\n", max_num_of_references);
        }
    }
    
    /* Allocate space to simulate the main memory */
    Frame *main_memory;
    main_memory = (Frame *)malloc(num_of_frames * sizeof(Frame));
    if (main_memory == NULL) {
        printf("An error occured during memory allocation\n");
        return ERROR;
    }
    
    /* Allocate space for the Inverted Page Table (IPT) */
    IPT_Entry *IPT;
    IPT = (IPT_Entry *)malloc(num_of_frames * sizeof(IPT_Entry));
    if (IPT == NULL) {
        printf("An error occured during memory allocation\n");
        return ERROR;
    }
    
    /* Initialize the IPT's entries */
    for (int frame = 0; frame < num_of_frames; frame++) {
        IPT[frame].valid = FALSE;  /* Initialy the information in the entries is invalid (trash) */
    }
    
    int *ws_bzip, *ws_gcc;  /* Each process has its own working set */
    if (strcmp(algorithm, "WS") == 0) {
        /* Allocate space for each working set */
        ws_bzip = (int *)malloc(ws_size * sizeof(int));
        if (ws_bzip == NULL) {
            printf("An error occured during memory allocation\n");
            return ERROR;
        }
        ws_gcc = (int *)malloc(ws_size * sizeof(int));
        if (ws_gcc == NULL) {
            printf("An error occured during memory allocation\n");
            return ERROR;
        }
        /* Initialize the working sets */
        for (int i = 0; i < ws_size; i++) {
            ws_bzip[i] = INVALID;  /* Initially each slot contains trash (not a valid page number) */
            ws_gcc[i] = INVALID;  /* The same applies here aswell */
        }
    }
    
    printf("\nSimulation:\n");
    int reference_count = 0;  /* Count the currently resolved references (both files included) */
    int load_count = 0;  /* Count how many times it was necessary to load page from hard disk to main memory (both files included) */
    int save_count = 0;  /* Count how many times it was necessary to save page from main memory to hard disk (both files included) */
    int pf_bzip = 0;  /* Count how many references of bzip led to page fault */
    int pf_gcc = 0;  /* Count how many references of gcc led to page fault */
    int references_bzip = 0;  /* Count how many references of bzip have been resolved so far */
    int references_gcc = 0;  /* Count how many references of gcc have been resolved so far */
    FILE *bzip, *gcc;
    bzip = fopen("bzip.trace", "r");  /* Open bzip file */
    gcc = fopen("gcc.trace", "r");  /* Open gcc file */
    char buffer[BUFFER_SIZE];  /* Buffer to store the current reference */
    for (int file = 0; ; file++) {  /* An integer that increases in order to repeatedly switch between these two files (using modulo) */
        switch (file % NUM_OF_FILES) {  /* This determines whose turn is to continue resolving references */
            case BZIP:  /* Half of the times it is bzip's turn */
                if (feof(bzip))  /* If there are no more bzip references to resolve, stop this case */
                    break;
                for (int ref = 0; ref < q; ref++) {  /* Resolve q references of bzip */
                    references_bzip++;  /* Resolving one more reference of bzip */
                    reference_count++;  /* Therefore resolving one more reference overall */
                    printf("Reference %d of bzip (%d overall): ", references_bzip, reference_count);
                    fread(buffer, sizeof(buffer), 1, bzip);  /* Read from file as much as you need to know about this reference */
                    Print_Not_Null_Terminated_String(buffer, BUFFER_SIZE);  /* buffer doesn't contain a proper string so %s identifier would cause undefined behavior */
                    Reference reference = TranslateBuffer(buffer);  /* Turn the information stored in buffer to an actual reference */
                    int frame_pos = INVALID;  /* This will show which frame hosts the requested page */
                    for (int frame = 0; frame < num_of_frames; frame++) {  /* Scan the contents of each frame (via the corresponding IPT's entry) */
                        if (IPT[frame].valid == TRUE && IPT[frame].pid == BZIP && IPT[frame].page_num == reference.page_num) {  /* The requested page is already loaded (best case scenario) */
                            frame_pos = frame;  /* Save the position of the frame that hosts the requested page */
                            break;
                        }
                    }
                    if (frame_pos == INVALID) {  /* The requested page was not found in any frame, so we need to find a frame to load it */
                        pf_bzip++;  /* That means a page fault occured due to this reference of bzip */
                        for (int frame = 0; frame < num_of_frames; frame++) {  /* Scan the contents of each frame (via the corresponding IPT's entry) */
                            if (IPT[frame].valid == FALSE) {  /* If an empty frame was found, load the page here */
                                printf("LOAD page %d from hard disk to frame %d of main memory\n", reference.page_num, frame);
                                load_count++;  /* Increase by 1 the number of loads */
                                /* Update the corresponding IPT's entry */
                                IPT[frame].pid = BZIP;
                                IPT[frame].page_num = reference.page_num;
                                IPT[frame].modified = FALSE;
                                IPT[frame].valid = TRUE;
                                frame_pos = frame;  /* Save the position of the frame that hosts the requested page */
                                break;
                            }
                        }
                    }
                    if (frame_pos == INVALID) {  /* There wasn't any available frame (main memory is full) so page replacement required */
                        if (strcmp(algorithm, "LRU") == 0) {
                            int frame_with_min_timestamp = 0;  /* This will show the frame that hosts the page with the min timestamp */
                            for (int frame = 0; frame < num_of_frames; frame++) {  /* Scan the contents of each frame (via the corresponding IPT's entry) */
                                if (IPT[frame].timestamp < IPT[frame_with_min_timestamp].timestamp)  /* If the frame hosts a page with timestamp less than the min so far */
                                    frame_with_min_timestamp = frame;  /* Save its position */
                            }
                            frame_pos = frame_with_min_timestamp;  /* Save the position of the frame that will host the requested page */
                        }
                        else if (strcmp(algorithm, "WS") == 0) {
                            for (int frame = 0; frame < num_of_frames; frame++) {  /* Scan the contents of each frame (via the corresponding IPT's entry) */
                                if ((IPT[frame].pid == BZIP && !WS_Includes_This_Page(ws_bzip, ws_size, IPT[frame].page_num)) ||
                                        (IPT[frame].pid == GCC && !WS_Includes_This_Page(ws_gcc, ws_size, IPT[frame].page_num))) {  /* If none of the working sets doesn't include the hosted page of a frame */
                                    frame_pos = frame;  /* This page will be replaced by the requested one, so save the position of this frame */
                                    break;
                                }
                            }
                            if (frame_pos == INVALID) {  /* If there was not such a page that doesn't belongs to neither of the working sets, disturb gcc's woking set */
                                printf("NOTE: Due to memory restriction bzip had to disturb gcc's working set in order to keep running\n");
                                for (int frame = 0; frame < num_of_frames; frame++) {  /* Scan the contents of each frame (via the corresponding IPT's entry) */
                                    if (IPT[frame].pid == GCC) {  /* If the hosted page belongs to gcc */
                                        WS_Remove_Page(ws_gcc, ws_size, IPT[frame].page_num);  /* Remove this page from gcc's working set */
                                        frame_pos = frame;  /* This page will be replaced by the requested one, so save the position of this frame */
                                        break;
                                    }
                                }
                            }
                            if (frame_pos == INVALID) {  /* If even that didn't solve the problem, the frames are too few to support working sets of this size */
                                printf("ERROR: Given working set size (%d) cannot be satisfied by %d frames\n", ws_size, num_of_frames);
                                return ERROR;
                            }
                        }
                        if (IPT[frame_pos].modified == TRUE) {  /* If the page that is going to be replaced has been modified, save it to hard disk */
                            printf("SAVE page %d from frame %d of main memory to hard disk\n", IPT[frame_pos].page_num, frame_pos);
                            save_count++;  /* Increase by 1 the number of saves */
                        }
                        printf("LOAD page %d from hard disk to frame %d of main memory\n", reference.page_num, frame_pos);
                        load_count++;  /* Increase by 1 the number of loads */
                        /* Update the corresponding IPT's entry */
                        IPT[frame_pos].pid = BZIP;
                        IPT[frame_pos].page_num = reference.page_num;
                        IPT[frame_pos].modified = FALSE;
                    }
                    IPT[frame_pos].timestamp = reference_count;  /* Using reference_count so the timestamps of two consecutive references differ by 1 */
                    Frame *target_frame = main_memory + frame_pos;  /* The frame that hosts the requested page */
                    char *target_data = ((char *)target_frame) + reference.offset;  /* The specified data to perform the action (READ or WRITE) */
                    switch(reference.action) {
                        case 'R':  /* "Read" */
                            printf("READ page %d from frame %d of main memory\n", reference.page_num, frame_pos);
                            break;
                        case 'W':  /* "Write" */
                            printf("WRITE page %d to frame %d of main memory\n", reference.page_num, frame_pos);
                            IPT[frame_pos].modified = TRUE;  /* The page has been "written" */
                            break;
                        default:
                            printf("Invalid reference detected in file bzip\n");
                            return ERROR;
                    }
                    if (strcmp(algorithm, "WS") == 0) {
                        WS_Insert_Page(ws_bzip, ws_size, reference.page_num);  /* Add this page to the working set of bzip */
                    }
                    if ((max_num_of_references != INVALID && reference_count == max_num_of_references) || feof(bzip))
                        break;  /* Stop this loop if the number of references reached the max or there are no more bzip references to resolve */
                    fgetc(bzip);  /* Skip end of line that follows 'R' or 'W' */
                }
                break;
                
            case GCC:  /* The other half of the times it is gcc's turn */
                if (feof(gcc))  /* If there are no more gcc references to resolve, stop this case */
                    break;
                for (int ref = 0; ref < q; ref++) {  /* Resolve q references of gcc */
                    references_gcc++;  /* Resolving one more reference of gcc */
                    reference_count++;  /* Therefore resolving one more reference overall */
                    printf("Reference %d of gcc (%d overall): ", references_gcc, reference_count);
                    fread(buffer, sizeof(buffer), 1, gcc);  /* Read from file as much as you need to know about this reference */
                    Print_Not_Null_Terminated_String(buffer, BUFFER_SIZE);  /* buffer doesn't contain a proper string so %s identifier would cause undefined behavior */
                    Reference reference = TranslateBuffer(buffer);  /* Turn the information stored in buffer to an actual reference */
                    int frame_pos = INVALID;  /* This will show which frame hosts the requested page */
                    for (int frame = 0; frame < num_of_frames; frame++) {  /* Scan the contents of each frame (via the corresponding IPT's entry) */
                        if (IPT[frame].valid == TRUE && IPT[frame].pid == GCC && IPT[frame].page_num == reference.page_num) {  /* The requested page is already loaded (best case scenario) */
                            frame_pos = frame;  /* Save the position of the frame that hosts the requested page */
                            break;
                        }
                    }
                    if (frame_pos == INVALID) {  /* The requested page was not found in any frame, so we need to find a frame to load it */
                        pf_gcc++;  /* That means a page fault occured due to this reference of gcc */
                        for (int frame = 0; frame < num_of_frames; frame++) {  /* Scan the contents of each frame (via the corresponding IPT's entry) */
                            if (IPT[frame].valid == FALSE) {  /* If an empty frame was found, load the page here */
                                printf("LOAD page %d from hard disk to frame %d of main memory\n", reference.page_num, frame);
                                load_count++;  /* Increase by 1 the number of loads */
                                /* Update the corresponding IPT's entry */
                                IPT[frame].pid = GCC;
                                IPT[frame].page_num = reference.page_num;
                                IPT[frame].modified = FALSE;
                                IPT[frame].valid = TRUE;
                                frame_pos = frame;  /* Save the position of the frame that hosts the requested page */
                                break;
                            }
                        }
                    }
                    if (frame_pos == INVALID) {  /* There wasn't any available frame (main memory is full) so page replacement required */
                        if (strcmp(algorithm, "LRU") == 0) {
                            int frame_with_min_timestamp = 0;  /* This will show the frame that hosts the page with the min timestamp */
                            for (int frame = 0; frame < num_of_frames; frame++) {  /* Scan the contents of each frame (via the corresponding IPT's entry) */
                                if (IPT[frame].timestamp < IPT[frame_with_min_timestamp].timestamp)  /* If the frame hosts a page with timestamp less than the min so far */
                                    frame_with_min_timestamp = frame;  /* Save its position */
                            }
                            frame_pos = frame_with_min_timestamp;  /* Save the position of the frame that will host the requested page */
                        }
                        else if (strcmp(algorithm, "WS") == 0) {
                            for (int frame = 0; frame < num_of_frames; frame++) {  /* Scan the contents of each frame (via the corresponding IPT's entry) */
                                if ((IPT[frame].pid == GCC && !WS_Includes_This_Page(ws_gcc, ws_size, IPT[frame].page_num)) ||
                                        (IPT[frame].pid == BZIP && !WS_Includes_This_Page(ws_bzip, ws_size, IPT[frame].page_num))) {  /* If none of the working sets doesn't include the hosted page of a frame */
                                    frame_pos = frame;  /* This page will be replaced by the requested one, so save the position of this frame */
                                    break;
                                }
                            }
                            if (frame_pos == INVALID) {  /* If there was not such a page that doesn't belongs to neither of the working sets, disturb bzip's woking set */
                                printf("NOTE: Due to memory restriction gcc had to disturb bzip's working set in order to keep running\n");
                                for (int frame = 0; frame < num_of_frames; frame++) {  /* Scan the contents of each frame (via the corresponding IPT's entry) */
                                    if (IPT[frame].pid == BZIP) {  /* If the hosted page belongs to bzip */
                                        WS_Remove_Page(ws_bzip, ws_size, IPT[frame].page_num);  /* Remove this page from bzip's working set */
                                        frame_pos = frame;  /* This page will be replaced by the requested one, so save the position of this frame */
                                        break;
                                    }
                                }
                            }
                            if (frame_pos == INVALID) {  /* If even that didn't solve the problem, the frames are too few to support working sets of this size */
                                printf("ERROR: Given working set size (%d) cannot be satisfied by %d frames\n", ws_size, num_of_frames);
                                return ERROR;
                            }
                        }
                        if (IPT[frame_pos].modified == TRUE) {  /* If the page that is going to be replaced has been modified, save it to hard disk */
                            printf("SAVE page %d from frame %d of main memory to hard disk\n", IPT[frame_pos].page_num, frame_pos);
                            save_count++;  /* Increase by 1 the number of saves */
                        }
                        printf("LOAD page %d from hard disk to frame %d of main memory\n", reference.page_num, frame_pos);
                        load_count++;  /* Increase by 1 the number of loads */
                        /* Update the corresponding IPT's entry */
                        IPT[frame_pos].pid = GCC;
                        IPT[frame_pos].page_num = reference.page_num;
                        IPT[frame_pos].modified = FALSE;
                    }
                    IPT[frame_pos].timestamp = reference_count;  /* Using reference_count so the timestamps of two consecutive references differ by 1 */
                    Frame *target_frame = main_memory + frame_pos;  /* The frame that hosts the requested page */
                    char *target_data = ((char *)target_frame) + reference.offset;  /* The specified data to perform the action (READ or WRITE) */
                    switch(reference.action) {
                        case 'R':  /* "Read" */
                            printf("READ page %d from frame %d of main memory\n", reference.page_num, frame_pos);
                            break;
                        case 'W':  /* "Write" */
                            printf("WRITE page %d to frame %d of main memory\n", reference.page_num, frame_pos);
                            IPT[frame_pos].modified = TRUE;  /* The page has been "written" */
                            break;
                        default:
                            printf("Invalid reference detected in file gcc\n");
                            return ERROR;
                    }
                    if (strcmp(algorithm, "WS") == 0) {
                        WS_Insert_Page(ws_gcc, ws_size, reference.page_num);  /* Add this page to the working set of gcc */
                    }
                    if ((max_num_of_references != INVALID && reference_count == max_num_of_references) || feof(gcc))
                        break;  /* Stop this loop if the number of references reached the max or there are no more gcc references to resolve */
                    fgetc(gcc);  /* Skip end of line that follows 'R' or 'W' */
                }
                break;
        }
        if ((max_num_of_references != INVALID && reference_count == max_num_of_references) || (feof(bzip) && feof(gcc)))
            break;
    }
    
    fclose(bzip);  /* Close bzip file */
    fclose(gcc);  /* Close gcc file */
    
    int used_frames = 0;  /* The number of frames that hosted atleast one page during this simulation */
    for (int frame = 0; frame < num_of_frames; frame++) {  /* Scan the contents of each frame (via the corresponding IPT's entry) */
        if (IPT[frame].valid == TRUE)  /* If the frame contains valid information */
            used_frames++;  /* It has been used at some point */
    }
    
    /* Release the allocated memory */
    if (strcmp(algorithm, "WS") == 0) {
        free(ws_bzip);
        free(ws_gcc);
    }
    free(IPT);
    free(main_memory);
    
    printf("\nResults:\n");
    printf("LOAD from hard disk to main memory (aka read from HD): %d pages\n", load_count);
    printf("SAVE from main memory to hard disk (aka write to HD): %d pages\n", save_count);
    printf("bzip: %d page faults, %d resolved references\n", pf_bzip, references_bzip);
    printf("gcc: %d page faults, %d resolved references\n", pf_gcc, references_gcc);
    printf("During this simulation: %d frames were used of %d available frames\n\n", used_frames, num_of_frames);
    return OK;  /* Success */
}
