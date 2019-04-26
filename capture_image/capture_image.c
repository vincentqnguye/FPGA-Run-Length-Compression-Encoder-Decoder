// #include "address_map_arm.h"
#include "hps.h"
#include "hps_soc_system.h"
#include "socal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define KEY_BASE         0xFF200050
#define SWITCH_BASE      0xFF200040
#define VIDEO_IN_BASE    0xFF203060
#define FPGA_ONCHIP_BASE 0xC8000000
#define CHARACTER_BUFFER 0xC9000000
#define SDRAM_BEGIN 	 0xC0000000

/* 
 *	ECE 354 - Computer Systems Lab II
 * 	Lab 4
 *  
 *  Written by: Vincent Nguyen, Arthur Singas, Macy Jabbour
 *
 */
uint8_t to_send = 0;
int image[76800];
uint8_t processed_image[9600];

void decompress(int outputs, volatile short *video_mem, volatile int * SDRAM_ptr)
{
	int input = 0; 											/* Full 24 bits of RLE output */
	int value_bit = 0; 										/* 1st bit of 24, represents color of pixel */
	int occur = 0; 											/* Number of occurences of current value bit */ 
	int SDRAM_off = 0;										/* SDRAM offset */
	int i, j;
	int x = 0;
	int y = 0;												/* Current Y-value on screen (pixel) */
	
	for(i = 0; i < outputs; i++){							/* Loop thru all RLE outputs */
		input = *(SDRAM_ptr + SDRAM_off);
		SDRAM_off++;
		value_bit = (input & 0x00800000) >> 23; 			/* Get the 24th bit (msb) which is the first value */
		occur = input & 0x007FFFFF;   						/* second value is the first 23 bits of compressed input */
		
		for(j = 0; j < occur; j++){ 
			if (value_bit == 1){							/* If value_bit == 1 -> print black pixel to screen */
				*(video_mem + (y << 9) + x-8) = 0x0000; 
			} else {										/* If value_bit isnt 1 -> print white pixel to screen */
				*(video_mem + (y << 9) + x-8) = 0xFFFF; 
			} 
			x = x + 1;
			if(x == 320 && y < 239){
				x = 0;
				y = y + 1;
			}
			else if(x == 320 && y == 239){
				y = y + 1;
			}
		}
	}
	printf("Compressed Image Size: %d bytes\n", 3*outputs);
	printf("Decompressed Image Size: %d bytes\n", 9600);
	printf("Compression Ratio: %f\n", 9600/(3*(float)outputs));
}

void black_screen(volatile short *video_mem)
{
	int i = 0;
	int bit_count = 7;
	int offset = 0;
	int x, y;
	
	for (y = 0; y < 240; y++) {
		for (x = 0; x < 320; x++) {
			short temp = *(video_mem + (y << 9) + x);
			*(video_mem + (y << 9) + x) = 0x0000; 
		}
	}
}

int compression(volatile int * SDRAM_ptr)
{
	/* reset before compression */
	alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST + RLE_RESET_BASE, 1);
	alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST + RLE_RESET_BASE, 0);
	
	int SDRAM_offset = 0;
	int byte_count = 0;
	long output = 0;
	int count_outputs = 0;
	
	int secVal = 0;
	int firstVal = 0;
	
	while(byte_count < 9600) // 9600 bytes to compress
	{
		if(alt_read_byte(ALT_FPGA_BRIDGE_LWH2F_OFST + FIFO_IN_FULL_PIO_BASE) == 0) {							/* if the fifo is not full, send 8 bits */
			alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST + FIFO_IN_WRITE_REQ_PIO_BASE, 1);		
			alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST + ODATA_PIO_BASE, processed_image[byte_count]); 			/* sets input_data*/
			alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST + FIFO_IN_WRITE_REQ_PIO_BASE, 0);							/* stores input_data */
			byte_count++;
		}
		if(alt_read_byte(ALT_FPGA_BRIDGE_LWH2F_OFST + RESULT_READY_PIO_BASE) == 0) { 							/* if RLE has output, read output */
			alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST + FIFO_OUT_READ_REQ_PIO_BASE, 1);
			output = alt_read_word(ALT_FPGA_BRIDGE_LWH2F_OFST + IDATA_PIO_BASE);
			*(SDRAM_ptr + SDRAM_offset) = output;
			SDRAM_offset++;
			alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST + FIFO_OUT_READ_REQ_PIO_BASE, 0);
			count_outputs++;
		}
	}
	alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST + RLE_FLUSH_PIO_BASE, 1);
	alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST + RLE_FLUSH_PIO_BASE, 0);
	return count_outputs;
}

/* Creates a black and white version of the image */
void black_white(volatile short *video_mem)
{
	int i = 0;
	int bit_count = 0;
	int offset = 0;
	int x, y;
	int process_count = 0;
	
	for (y = 0; y < 240; y++) {
		for (x = 0; x < 320; x++) {
			short temp = *(video_mem + (y << 9) + x);
			if(temp < 0x2211) { 																			/* Threshold between black and white */
				temp = 0xFFFF; 
				*(video_mem + (y << 9) + x) = temp; 														/* Sets pixel to white if under */																
				image[i] = 0;
				i++;
			}
			else {
				temp = 0x0000;
				*(video_mem + (y << 9) + x) = temp; 														/* sets pixel to black if under */											
				image[i] = 1;
				i++;
			}
		}
	}
	
	for (i = 0; i < 76800; i++){
		to_send |= image[i] << bit_count;
		bit_count++;
		if(bit_count == 8) {
			
			processed_image[process_count] = to_send;
			process_count++;
			offset += 1;
			to_send = 0;
			bit_count = 0;
		}
	}
}

/* Takes initial image to be processed */
void take_picture(volatile short *video_mem)
{
	int x, y;
	
	for (y = 0; y < 240; y++) {
		for (x = 0; x < 320; x++) {
			short temp = *(video_mem + (y << 9) + x);
			*(video_mem + (y << 9) + x) = temp;
		}
	}
}

int main(void)
{
	volatile int * KEY_ptr = (int *) KEY_BASE;
    volatile long * Switch_ptr = (long *) SWITCH_BASE;
	volatile int * Video_In_DMA_ptr	= (int *) VIDEO_IN_BASE;
	volatile short * Video_Mem_ptr	= (short *) FPGA_ONCHIP_BASE;
	volatile char * Char_Buffer_ptr = (int *) CHARACTER_BUFFER;
	volatile int * SDRAM_BASE_ptr	= (int *) SDRAM_BEGIN;

	*(Video_In_DMA_ptr + 3)	= 0x4;																	/* Enable the video */
	int switch_val = 0;	
	int blackwhite = 1;
	int compress = 1;
	int black = 1;
	int RLE_outputs = 0;

    while(1) {
        switch_val = *(Switch_ptr);
        if(switch_val == 0x1) { 
            *(Video_In_DMA_ptr + 3)	= 0x0;															/* Disable the video */
            take_picture(Video_Mem_ptr);
        }
        else if(switch_val == 0x3 && blackwhite == 1) {
            black_white(Video_Mem_ptr);
			//printf("%s\n", "blackwhite");
			blackwhite = 0;
        }
		
		else if(switch_val == 0x7 && black == 1) {
			printf("%s\n", "compressing");
			black_screen(Video_Mem_ptr);
			black = 0;
		}
		
		else if(switch_val == 0xF && compress == 1) {
			RLE_outputs = compression(SDRAM_BASE_ptr);
			decompress(RLE_outputs, Video_Mem_ptr, SDRAM_BASE_ptr);
			compress = 0;
		}
		
        else if(switch_val == 0x0){
            *(Video_In_DMA_ptr + 3)	= 0x4;
			switch_val = *(Switch_ptr);
			blackwhite = 1;
			black = 1;
			compress = 1;
		}
	}
}