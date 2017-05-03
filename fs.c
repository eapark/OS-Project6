
#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024
#define BYTES_PER_BLOCK 4096



struct fs_superblock {
	int magic;
	int nblocks;
	int ninodeblocks;
	int ninodes;
};

struct fs_inode {
	int isvalid;
	int size;
	int direct[POINTERS_PER_INODE];
	int indirect;
};

union fs_block {
	struct fs_superblock super;
	struct fs_inode inode[INODES_PER_BLOCK];
	int pointers[POINTERS_PER_BLOCK];
	char data[DISK_BLOCK_SIZE];
};


// Global Variables

_Bool fs_mounted = 0;
_Bool *bitmap;
int bitmapSize = 0;


// prototypes

int getNewInode(void);


int fs_format()
{
	if(fs_mounted == 1)
	{
		printf("Formatting Error: Can't format a mounted FS\n");
		return 0;
		
	}
	int blocks = disk_size();
	int ninode_blocks = blocks / 10;
	if(blocks < 3)
	{
		printf("Not enough blocks to build a file system!\n");
		return 0;
	}
	if(ninode_blocks != 0){
		if(blocks % ninode_blocks != 0)
			ninode_blocks++; // Round up
	}
	else
	{
		ninode_blocks = 1;
	}
	union fs_block block;
	disk_read(0,block.data);

	// Format super
	block.super.magic = FS_MAGIC;
	block.super.nblocks = blocks;
	block.super.ninodeblocks = ninode_blocks;
	block.super.ninodes = ninode_blocks*INODES_PER_BLOCK;

	disk_write(0,block.data);

	// Invalidate all inodes
	
	int i;
	int j;
	union fs_block inode_block;
	for(i = 0; i < ninode_blocks; i++)
	{
		disk_read(i+1, inode_block.data);
		
		for(j = 0; j < INODES_PER_BLOCK ; j++)
		{
			inode_block.inode[j].isvalid = 0;
		}

		disk_write(i+1,inode_block.data);

	}

	return 1;
}

void fs_debug()
{
	union fs_block block;

	disk_read(0,block.data);

	printf("superblock:\n");
	printf("\t%d blocks\n",block.super.nblocks);
	printf("\t%d inode blocks\n",block.super.ninodeblocks);
	printf("\t%d inodes\n",block.super.ninodes);
	
	int i;
	int j;
	int k;
	union fs_block inode_blocks[block.super.ninodeblocks];
	for(i = 1; i <= block.super.ninodeblocks; i++){ 
		disk_read(i, inode_blocks[i-1].data);
		for(j = 0; j < INODES_PER_BLOCK ; j++){
			if(inode_blocks[i-1].inode[j].isvalid == 1){
				printf("inode %d:\n",j+INODES_PER_BLOCK*(i-1));
				int size = inode_blocks[i-1].inode[j].size;
				printf("\tsize: %d bytes\n",size);
				int nblocks = size/BYTES_PER_BLOCK;
				if(size%BYTES_PER_BLOCK != 0)
					nblocks += 1;

				int direct_blocks = ( nblocks > POINTERS_PER_INODE ? POINTERS_PER_INODE : nblocks);
				int indirect_blocks = 0;
				if(nblocks > POINTERS_PER_INODE)
					indirect_blocks = nblocks - POINTERS_PER_INODE;
				if(indirect_blocks > POINTERS_PER_BLOCK)
				{
					printf("Size exceeds FileSystem Capability\n");
					return ;
				}
				printf("\tdirect blocks:");
				for(k = 0; k < direct_blocks; k++)
				{
					printf(" %d",inode_blocks[i-1].inode[j].direct[k]);	

				}
				printf("\n");

				if( indirect_blocks > 0)
				{
					printf("\tindirect block: %d\n",inode_blocks[i-1].inode[j].indirect);
					printf("\tindirect data blocks:");
				
					union fs_block pointers_block;
					disk_read(inode_blocks[i-1].inode[j].indirect, pointers_block.data); 		
					for(k = 0; k < indirect_blocks; k++)
					{
						printf(" %d",pointers_block.pointers[k]);
						
					}
					printf("\n");

				}
			}
		}
	}
}

void print_bitmap(void)
{
	int i;
	int nl = 0;
	for(i = 0; i < bitmapSize; i++)
	{
		printf("%d:%d,", i,bitmap[i]);
		nl +=1;
		if(nl > 10){
			printf("\n");
			nl = 0;
		}
	}
	printf("\n");

}


int fs_mount()
{
	union fs_block block;

	disk_read(0,block.data);
	// Check Magic
	if(block.super.magic != FS_MAGIC)
	{
		printf("Did not find proper filesystem. Operation Failed\n");
		return 0;
	}
	
	if(bitmap != NULL)
	{
		free(bitmap);
		bitmap = NULL;
	}
	bitmap = malloc((block.super.nblocks) * sizeof(_Bool));
	
	// Initialize bitmap to zero
	
	int b;
	for(b = 0; b < block.super.nblocks; b++)
	{
		bitmap[b] = 0;

	}

	bitmap[0] = 1;


	int diskSize = disk_size();

	// Read used data blocks
	int i;
	int j;
	int k;
	
	union fs_block inode_blocks[block.super.ninodeblocks];
	for(i = 1; i <= block.super.ninodeblocks; i++){
		bitmap[i] = 1;
		disk_read(i, inode_blocks[i-1].data);
		for(j = 0; j < INODES_PER_BLOCK ; j++){
			if(inode_blocks[i-1].inode[j].isvalid == 1){
				int size = inode_blocks[i-1].inode[j].size;
				int nblocks = size/BYTES_PER_BLOCK;
				if(size%BYTES_PER_BLOCK != 0)
					nblocks += 1;

				int direct_blocks = ( nblocks > POINTERS_PER_INODE ? POINTERS_PER_INODE : nblocks);
				int indirect_blocks = 0;
				if(nblocks > POINTERS_PER_INODE)
					indirect_blocks = nblocks - POINTERS_PER_INODE;
				if(indirect_blocks > POINTERS_PER_BLOCK)
				{
					printf("Error Mounting: A file with a too large size was detected.\n");
					free(bitmap);
					bitmap = NULL;
					return 0;
				}
				for(k = 0; k < direct_blocks; k++)
				{
					int blockNum = inode_blocks[i-1].inode[j].direct[k];
					if(blockNum <=block.super.ninodeblocks || blockNum >= diskSize )
					{
						printf("Error Mounting FS: Invalid block number detected in Filesystem.\n");
						free(bitmap);
						bitmap = NULL;
						return 0;
					}
					bitmap[blockNum] = 1;		
				}

				if( indirect_blocks > 0)
				{
					bitmap[inode_blocks[i-1].inode[j].indirect] = 1;
					union fs_block pointers_block;
					disk_read(inode_blocks[i-1].inode[j].indirect, pointers_block.data); 		
					for(k = 0; k < indirect_blocks; k++)
					{	
						int blockNum = pointers_block.pointers[k];
						if(blockNum <=block.super.ninodeblocks || blockNum >= diskSize )
						{
							printf("Error Mounting FS: Invalid block number detected in Filesystem.\n");
							free(bitmap);
							bitmap = NULL;
							return 0;
						}
						bitmap[blockNum] = 1;
						
					}

				}
			}
		}
	}
	fs_mounted = 1;	
	bitmapSize = block.super.nblocks;
	//print_bitmap();
	return 1;
}

int fs_create()
{
	if(!fs_mounted)
	{
		printf("No mounted filesystem found\n");
		return 0;
	}
	union fs_block super;

	union fs_block inodeB;
	disk_read(0, super.data);

	int i;
	int j;
	int foundInode = -1;
	_Bool found = 0;
	for(i= 0; i < super.super.ninodeblocks;i++)
	{
		disk_read(i+1, inodeB.data);
		for(j = 0; j < INODES_PER_BLOCK; j++)
		{
			if(!inodeB.inode[j].isvalid && j+i != 0)
			{
				found = 1;
				foundInode = j;
				break;
			}

		}
		if(found)
			break;
	}
		
		
	// Create inode
	if(found)
	{
		inodeB.inode[foundInode].isvalid = 1;
		inodeB.inode[foundInode].size = 0;
		disk_write(i+1, inodeB.data);
		return foundInode + i*INODES_PER_BLOCK;
	}
	else
	{
		return 0;
	}
}

int fs_delete( int inumber )
{
	if(!fs_mounted)
	{
		printf("No mounted filesystem found\n");
		return 0;
	}
	union fs_block super;

	union fs_block inodeB;
	disk_read(0, super.data);

	int inodeBlock = inumber/INODES_PER_BLOCK;
	if(inodeBlock > super.super.ninodeblocks || inumber > super.super.ninodes || inumber < 1){
		printf("Invalid inumber\n");
		return 0;
	}
	
	_Bool Error = 0;
	disk_read(inodeBlock + 1, inodeB.data);

	int inodeIndex = inumber - INODES_PER_BLOCK*inodeBlock;
	if(inodeB.inode[inodeIndex].isvalid)
	{	
		int k;
		int size = inodeB.inode[inodeIndex].size;
		int diskSize = disk_size();
		int nblocks = size/BYTES_PER_BLOCK;
		if(size%BYTES_PER_BLOCK != 0)
			nblocks += 1;

		int direct_blocks = ( nblocks > POINTERS_PER_INODE ? POINTERS_PER_INODE : nblocks);
		int indirect_blocks = 0;
		if(nblocks > POINTERS_PER_INODE)
			indirect_blocks = nblocks - POINTERS_PER_INODE;
		if(indirect_blocks > POINTERS_PER_BLOCK)
		{
			printf("Error Deleting Inode: A file with a too large size was detected.Possible corruption in filesystem. An attempt to fix the corruption will be made.\n");
			indirect_blocks = POINTERS_PER_BLOCK;
			Error = 1;
		}
		for(k = 0; k < direct_blocks; k++)
		{
			int blockNum = inodeB.inode[inodeIndex].direct[k];
			if(blockNum <=super.super.ninodeblocks || blockNum >= diskSize )
			{
				printf("Error Deleting: Invalid block number detected in Filesystem.\n");
				Error = 1;
			}
			else{
				bitmap[blockNum] = 0;
			}
		}

		if( indirect_blocks > 0)
		{
		
			union fs_block pointers_block;
			disk_read(inodeB.inode[inodeIndex].indirect, pointers_block.data); 		
			for(k = 0; k < indirect_blocks; k++)
			{	
				int blockNum = pointers_block.pointers[k];
				if(blockNum <=super.super.ninodeblocks || blockNum >= diskSize )
				{
					printf("Error Deleting Inode: Invalid block number detected in Filesystem.\n");
					Error = 1;
				}
				else{
					bitmap[blockNum] = 0;
				}
				
			}
			bitmap[inodeB.inode[inodeIndex].indirect] = 0; // free indirect block
		}


	}
	else{
		printf("Error Deleting Inode: The inode is invalid\n");
		return 0;

	}
	// Write to inode
	inodeB.inode[inodeIndex].size = 0;
	inodeB.inode[inodeIndex].isvalid = 0;
	disk_write(inodeBlock + 1, inodeB.data);
	if(Error)
	{
		printf("Inode was succesfully deleted, but there may be some corruption in data\n");
	}
	return 1;
}

int fs_getsize( int inumber )
{
	if(!fs_mounted)
	{
		printf("GetSize Error: No mounted filesystem found\n");
		return -1;
	}
	union fs_block super;

	union fs_block inodeB;
	disk_read(0, super.data);

	int inodeBlock = inumber/INODES_PER_BLOCK;
	if(inodeBlock > super.super.ninodeblocks || inumber > super.super.ninodes || inumber < 1){
		printf("GetSize Error: Invalid inumber\n");
		return -1;
	}
	
	disk_read(inodeBlock + 1, inodeB.data);

	int inodeIndex = inumber - INODES_PER_BLOCK*inodeBlock;
	if(inodeB.inode[inodeIndex].isvalid)
	{
		return inodeB.inode[inodeIndex].size;
	}
	else
	{
		printf("GetSize Error: Invalid Inode\n");
		return -1;
	}
	
}

int fs_read( int inumber, char *data, int length, int offset )
{
	if(!fs_mounted)
	{
		printf("No mounted filesystem found\n");
		return 0;
	}
	union fs_block super;
	int read = 0; // Bytes read

	union fs_block inodeB;
	disk_read(0, super.data);

	int inodeBlock = inumber/INODES_PER_BLOCK;
	if(inodeBlock > super.super.ninodeblocks || inumber > super.super.ninodes || inumber < 1){
		printf("Read Error: Invalid inumber\n");
		return 0;
	}
	
	disk_read(inodeBlock + 1, inodeB.data);

	int inodeIndex = inumber - INODES_PER_BLOCK*inodeBlock;
	if(inodeB.inode[inodeIndex].isvalid)
	{	
		int size = inodeB.inode[inodeIndex].size;
		
		if(length + offset > size)
		{
			length = size - offset;
		}
		
		if(offset > size)
		{
			return 0;
		}

		
		int diskSize = disk_size();
		int k;
		int nblocks = size/BYTES_PER_BLOCK;
		int startDirectByte = 0;
		int startIndirectByte = 0;
		int startDirectBlock;
		int startIndirectBlock;
		if(size%BYTES_PER_BLOCK != 0)
			nblocks += 1;
	
		startDirectByte = offset%BYTES_PER_BLOCK;
		startIndirectByte = (offset-(BYTES_PER_BLOCK*POINTERS_PER_INODE))%BYTES_PER_BLOCK;
		if(startIndirectByte < 0)
			startIndirectByte = 0;

		
		startDirectBlock = offset/BYTES_PER_BLOCK;
		startIndirectBlock = (offset - (BYTES_PER_BLOCK*POINTERS_PER_INODE))/BYTES_PER_BLOCK;
		if(startIndirectBlock < 0)
			startIndirectBlock = 0;



		int direct_blocks = ( nblocks > POINTERS_PER_INODE ? POINTERS_PER_INODE : nblocks);
		int indirect_blocks = 0;
		int to_read = 0;
		if(nblocks > POINTERS_PER_INODE)
			indirect_blocks = nblocks - POINTERS_PER_INODE;
		if(indirect_blocks > POINTERS_PER_BLOCK)
		{
			printf("Error Reading Inode: A file with a too large size was detected.Possible corruption in filesystem. An attempt to read will be made.\n");
			indirect_blocks = POINTERS_PER_BLOCK;
		}
		//printf("startDirectBlock:%d\nstartIndirectBlock:%d\ndirect_blocks:%d\nindirect_blocks:%d\nlength:%d\noffset:%d\nsize:%d\n",startDirectBlock, startIndirectBlock, direct_blocks, indirect_blocks, length, offset, size);
		for(k = startDirectBlock; k < direct_blocks && read < length; k++)
		{
			union fs_block readBlock;
			int blockNum = inodeB.inode[inodeIndex].direct[k];
			// printf("Reading block %d\n",blockNum);
			if(blockNum <=super.super.ninodeblocks || blockNum >= diskSize )
			{
				printf("Error Reading: Invalid block number detected in Filesystem.\n");
				return read;
			}
			else{
				if(startDirectByte > 0)
				{
					memmove(readBlock.data, &readBlock.data[startDirectByte], BYTES_PER_BLOCK-startDirectByte);
					to_read = ((length-read) > BYTES_PER_BLOCK - startDirectByte) ? (BYTES_PER_BLOCK-startDirectByte) : length-read;
					startDirectByte = 0;
				}
				else
				{
					to_read = ((length - read) > BYTES_PER_BLOCK) ? BYTES_PER_BLOCK : length-read;
				}
		//		printf("to_read is: %d\nread is:%d\nlength is:%d\n",to_read, read, length);
		//		printf("disk_read segfaults\n");
				disk_read(blockNum, readBlock.data);
		//		printf("strncat segfaults\n");
				memcpy(&data[read], readBlock.data, to_read);
		//		printf("It doesn't\n");
				read += to_read;
			}
		}

		if( indirect_blocks > 0)
		{
		//	printf("reading indirect\n");
		//	printf("startDirectBlock:%d\nstartIndirectBlock:%d\ndirect_blocks:%d\nindirect_blocks:%d\nlength:%d\noffset:%d\nsize:%d\nread:%d\n",startDirectBlock, startIndirectBlock, direct_blocks, indirect_blocks, length, offset, size, read);
			union fs_block readBlock;	
			union fs_block pointers_block;
			disk_read(inodeB.inode[inodeIndex].indirect, pointers_block.data); 		
			for(k = startIndirectBlock; k < indirect_blocks && read < length; k++)
			{	
				int blockNum = pointers_block.pointers[k];
				//printf("Reading block %d\n",blockNum);
				if(blockNum <=super.super.ninodeblocks || blockNum >= diskSize )
				{
					printf("Error Reading: Invalid block number detected in Filesystem.\n");
					return read;
				}
				else{
					if(startIndirectByte > 0)
					{
						memmove(readBlock.data, &readBlock.data[startIndirectByte], BYTES_PER_BLOCK-startIndirectByte);
						to_read = ((length-read) > BYTES_PER_BLOCK - startIndirectByte) ? (BYTES_PER_BLOCK-startIndirectByte) : length-read;
						startIndirectByte = 0;
					}
					else
					{
						to_read = ((length - read) > BYTES_PER_BLOCK) ? BYTES_PER_BLOCK : length-read;
					}
					disk_read(blockNum, readBlock.data);
					memcpy(&data[read], readBlock.data, to_read);
					read += to_read;
				}
				
			}
		}

	}
	else{
		printf("Error Reading: The inode is invalid\n");
		return 0;

	}
	return read;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	// Check Mounted
	if(!fs_mounted)
	{
		printf("No mounted filesystem found\n");
		return 0;
	}


	// Check Inumber
	union fs_block super;
	int size;
	union fs_block inodeB;
	disk_read(0, super.data);
	
	int inodeBlock = inumber/INODES_PER_BLOCK;
	if(inodeBlock > super.super.ninodeblocks || inumber > super.super.ninodes || inumber < 1){
		printf("Write Error: Invalid inumber\n");
		return 0;
	}
	
	// Read Inode
	disk_read(inodeBlock + 1, inodeB.data);
	int written = 0;

	int inodeIndex = inumber - INODES_PER_BLOCK*inodeBlock;
	if(inodeB.inode[inodeIndex].isvalid)
	{	
		// Find size and test offset
		size = inodeB.inode[inodeIndex].size;
	

		// Find starting bytes and blocks
		int diskSize = disk_size();
		int k;
		int nblocks = size/BYTES_PER_BLOCK;
		int startDirectByte = 0;
		int startIndirectByte = 0;
		int startDirectBlock;
		int startIndirectBlock;
		_Bool allocateIndirectBlock = 0;
		if(size%BYTES_PER_BLOCK != 0)
			nblocks += 1;
	
		startDirectByte = offset%BYTES_PER_BLOCK;
		startIndirectByte = (offset-(BYTES_PER_BLOCK*POINTERS_PER_INODE))%BYTES_PER_BLOCK;
		if(startIndirectByte < 0)
			startIndirectByte = 0;
		
		startDirectBlock = offset/BYTES_PER_BLOCK;
		startIndirectBlock = (offset - (BYTES_PER_BLOCK*POINTERS_PER_INODE))/BYTES_PER_BLOCK;
		if(startIndirectBlock < 0)
			startIndirectBlock = 0;


		
		// Find blocks to allocate
		int bytesToAllocate = ((offset + length) - nblocks*BYTES_PER_BLOCK);
		
		int blocksToAllocate = bytesToAllocate/BYTES_PER_BLOCK;
		if(bytesToAllocate % BYTES_PER_BLOCK != 0)
			blocksToAllocate += 1;
		int directAllocateIndex = POINTERS_PER_INODE - 1;
		int indirectAllocateIndex = -1;
		if(nblocks >= POINTERS_PER_INODE)
		{
			indirectAllocateIndex = nblocks - POINTERS_PER_INODE - 1;
		}
		else
		{
			directAllocateIndex = nblocks -1;
		}
		int onblocks = nblocks;
		nblocks += blocksToAllocate;
		
		if(onblocks <= POINTERS_PER_INODE && nblocks > POINTERS_PER_INODE)
		{	
			allocateIndirectBlock = 1;
		}
		if(offset > BYTES_PER_BLOCK*onblocks)
		{
			offset = BYTES_PER_BLOCK*onblocks;
		}

		// Find blocks to load
		int direct_blocks = ( nblocks > POINTERS_PER_INODE ? POINTERS_PER_INODE : nblocks);
		int indirect_blocks = 0;
		
		int to_write = 0;
		if(nblocks > POINTERS_PER_INODE)
			indirect_blocks = nblocks - POINTERS_PER_INODE;
		if(indirect_blocks > POINTERS_PER_BLOCK)
		{
			printf("Error Reading Inode: A file with a too large size was detected.Possible corruption in filesystem. An attempt to write will be made.\n");
			indirect_blocks = POINTERS_PER_BLOCK;
		}

		
		_Bool changedInodeBlock = 0;
		_Bool changedPointersBlock = 0;
		_Bool ranOutOfMemory = 0;
		int blockNum;
		for(k = startDirectBlock; k < direct_blocks && written < length; k++)
		{
			union fs_block writeBlock;
			if(k > directAllocateIndex){

				blockNum = getNewInode();
				if(blockNum < 0)
				{
					ranOutOfMemory = 1;
					printf("System has run out of memory. Please delete some files to free memory\n");
				}
				else{
					inodeB.inode[inodeIndex].direct[k] = blockNum;
					changedInodeBlock = 1;
				}
			}
			else
			{
				blockNum = inodeB.inode[inodeIndex].direct[k];
			}
			if(blockNum <=super.super.ninodeblocks || blockNum >= diskSize)
			{
				if(!ranOutOfMemory)
					printf("Error Writing: Invalid block number detected in Filesystem.\n");
			}
			else{
				if(startDirectByte > 0)
				{
					to_write = ((length-written) > BYTES_PER_BLOCK - startDirectByte) ? (BYTES_PER_BLOCK-startDirectByte) : length-written;
					memcpy(&writeBlock.data[startDirectByte], &data[written], to_write); // written should always be 0 at this point
					startDirectByte = 0;
				}
				else
				{
					to_write = ((length - written) > BYTES_PER_BLOCK) ? BYTES_PER_BLOCK : length-written;
					memcpy(writeBlock.data, &data[written],to_write); 	
				}
				//printf("writeBlock data: %s\n", writeBlock.data);
				disk_write(blockNum, writeBlock.data);
				written += to_write;
			}
		}

		if(changedInodeBlock)
			disk_write(inodeBlock + 1, inodeB.data);

		if( indirect_blocks > 0 && !ranOutOfMemory)
		{
			union fs_block writeBlock;	
			union fs_block pointers_block;
			if(allocateIndirectBlock){
				int newInode = getNewInode();
				if(newInode < 0)
				{
					ranOutOfMemory = 1;
					printf("System has ran out of memory\n");
				}
				else
				{
					inodeB.inode[inodeIndex].indirect = newInode; // Will be written to disk later along size
					//printf("Allocating new indirect block:%d\n",inodeB.inode[inodeIndex].indirect);
				}
			}
			disk_read(inodeB.inode[inodeIndex].indirect, pointers_block.data); 		
			int blockNum;
			for(k = startIndirectBlock; k < indirect_blocks && written < length && !ranOutOfMemory; k++)
			{	
				if(k > indirectAllocateIndex){

					blockNum = getNewInode();
					if(blockNum < 0)
					{
						printf("System ran out of memory\n");
						ranOutOfMemory = 1;
						break;
					}
					pointers_block.pointers[k] = blockNum;
					changedPointersBlock = 1;
				}
				else
				{
					blockNum = pointers_block.pointers[k];
				}
				int blockNum = pointers_block.pointers[k];
				if(blockNum <=super.super.ninodeblocks || blockNum >= diskSize )
				{
					printf("Error Writing: Invalid block number detected in Filesystem.\n");
					return written;
				}
				else{
					if(startIndirectByte > 0)
					{
						to_write = ((length-written) > BYTES_PER_BLOCK - startIndirectByte) ? (BYTES_PER_BLOCK-startIndirectByte) : length-written;
						memcpy(&writeBlock.data[startIndirectByte], &data[written], to_write);
						startIndirectByte = 0;
					}
					else
					{
						to_write = ((length - written) > BYTES_PER_BLOCK) ? BYTES_PER_BLOCK : length-written;
						memcpy(writeBlock.data, &data[written],to_write); 	
					}
					disk_write(blockNum, writeBlock.data);
					written += to_write;
				}
				
			}

			if(changedPointersBlock)
				disk_write(inodeB.inode[inodeIndex].indirect, pointers_block.data);
		}

	}
	else{
		printf("Error Writing: The inode is invalid\n");
		return 0;

	}
	// Above we made sure bytes written would not exceed max file size
	int max_size = BYTES_PER_BLOCK*(POINTERS_PER_INODE + POINTERS_PER_BLOCK);
	if(offset+written > size){
		int new_size = (offset + written > max_size) ? max_size : offset + written;
		inodeB.inode[inodeIndex].size = new_size;
		disk_write(inodeBlock + 1, inodeB.data);
	}
	return written;
}


int getNewInode()
{
	int i;
	_Bool found = 0;
	for(i = 0; i<bitmapSize ; i++)
	{
		if(bitmap[i] == 0)
		{
			bitmap[i] = 1;
			found = 1;
			break;
		}
	}
	if(found)
		return i;
	else
		return -1;
	
}
