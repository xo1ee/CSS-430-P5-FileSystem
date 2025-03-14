// ============================================================================
// fs.c - user FileSytem API
// ============================================================================

#include "bfs.h"
#include "fs.h"

// ============================================================================
// Close the file currently open on file descriptor 'fd'.
// ============================================================================
i32 fsClose(i32 fd)
{
    i32 inum = bfsFdToInum(fd);
    bfsDerefOFT(inum);
    return 0;
}

// ============================================================================
// Create the file called 'fname'.  Overwrite, if it already exsists.
// On success, return its file descriptor.  On failure, EFNF
// ============================================================================
i32 fsCreate(str fname)
{
    i32 inum = bfsCreateFile(fname);
    if (inum == EFNF)
        return EFNF;
    return bfsInumToFd(inum);
}

// ============================================================================
// Format the BFS disk by initializing the SuperBlock, Inodes, Directory and
// Freelist.  On succes, return 0.  On failure, abort
// ============================================================================
i32 fsFormat()
{
    FILE *fp = fopen(BFSDISK, "w+b");
    if (fp == NULL)
        FATAL(EDISKCREATE);

    i32 ret = bfsInitSuper(fp); // initialize Super block
    if (ret != 0)
    {
        fclose(fp);
        FATAL(ret);
    }

    ret = bfsInitInodes(fp); // initialize Inodes block
    if (ret != 0)
    {
        fclose(fp);
        FATAL(ret);
    }

    ret = bfsInitDir(fp); // initialize Dir block
    if (ret != 0)
    {
        fclose(fp);
        FATAL(ret);
    }

    ret = bfsInitFreeList(); // initialize Freelist
    if (ret != 0)
    {
        fclose(fp);
        FATAL(ret);
    }

    fclose(fp);
    return 0;
}

// ============================================================================
// Mount the BFS disk.  It must already exist
// ============================================================================
i32 fsMount()
{
    FILE *fp = fopen(BFSDISK, "rb");
    if (fp == NULL)
        FATAL(ENODISK); // BFSDISK not found
    fclose(fp);
    return 0;
}

// ============================================================================
// Open the existing file called 'fname'.  On success, return its file
// descriptor.  On failure, return EFNF
// ============================================================================
i32 fsOpen(str fname)
{
    i32 inum = bfsLookupFile(fname); // lookup 'fname' in Directory
    if (inum == EFNF)
        return EFNF;
    return bfsInumToFd(inum);
}

// ============================================================================
// Read 'numb' bytes of data from the cursor in the file currently fsOpen'd on
// File Descriptor 'fd' into 'buf'.  On success, return actual number of bytes
// read (may be less than 'numb' if we hit EOF).  On failure, abort
// ============================================================================
i32 fsRead(i32 fd, i32 numb, void *buf)
{
    i8 bioBuf[BYTESPERBLOCK * 4];
    i8 *result = (i8 *)buf;
    int curs = fsTell(fd);
    int inum = bfsFdToInum(fd);
    int startFBN = curs / 512;
    int numBlksToRead = (numb / 512) + 1;

    Inode inode;
    bfsReadInode(inum, &inode);

    int bytesRead = 0; // Tracks how many bytes have been read successfully
    int numBytes = 0;  // Tracks how many bytes are read per block
    for (int fbn = startFBN; fbn < startFBN + numBlksToRead; fbn++)
    {
        bfsRead(inum, fbn, result);
        if (curs + bytesRead >= inode.size) // EOF hit
            numBytes = inode.size - (curs + bytesRead);
        else
            numBytes = (numb - bytesRead < 512) ? (numb - bytesRead) : 512;

        memcpy(bioBuf + bytesRead, result, numBytes);
        bytesRead += numBytes;
    }

    if (bytesRead > 0) // If bytes were read, copy to buf and return # of bytes read
    {
        memcpy(result, bioBuf, bytesRead);
        bfsSetCursor(inum, curs + bytesRead);
        return bytesRead;
    }

    return 0;
}

// ============================================================================
// Move the cursor for the file currently open on File Descriptor 'fd' to the
// byte-offset 'offset'.  'whence' can be any of:
//
//  SEEK_SET : set cursor to 'offset'
//  SEEK_CUR : add 'offset' to the current cursor
//  SEEK_END : add 'offset' to the size of the file
//
// On success, return 0.  On failure, abort
// ============================================================================
i32 fsSeek(i32 fd, i32 offset, i32 whence)
{

    if (offset < 0)
        FATAL(EBADCURS);

    i32 inum = bfsFdToInum(fd);
    i32 ofte = bfsFindOFTE(inum);

    switch (whence)
    {
    case SEEK_SET:
        g_oft[ofte].curs = offset;
        break;
    case SEEK_CUR:
        g_oft[ofte].curs += offset;
        break;
    case SEEK_END:
    {
        i32 end = fsSize(fd);
        g_oft[ofte].curs = end + offset;
        break;
    }
    default:
        FATAL(EBADWHENCE);
    }
    return 0;
}

// ============================================================================
// Return the cursor position for the file open on File Descriptor 'fd'
// ============================================================================
i32 fsTell(i32 fd)
{
    return bfsTell(fd);
}

// ============================================================================
// Retrieve the current file size in bytes.  This depends on the highest offset
// written to the file, or the highest offset set with the fsSeek function.  On
// success, return the file size.  On failure, abort
// ============================================================================
i32 fsSize(i32 fd)
{
    i32 inum = bfsFdToInum(fd);
    return bfsGetSize(inum);
}

void writeToBlock(i32 inum, i32 offset, i32 fbn, i32 numToWrite, i8 *buf)
{
    i8 writingBuf[BYTESPERBLOCK];
    bfsRead(inum, fbn, writingBuf); // copy pre-existing contents

    memcpy(writingBuf + offset, buf, numToWrite); // add changes/overwrites

    i32 startDBN = bfsFbnToDbn(inum, fbn);
    bioWrite(startDBN, writingBuf);
}

// ============================================================================
// Write 'numb' bytes of data from 'buf' into the file currently fsOpen'd on
// filedescriptor 'fd'.  The write starts at the current file offset for the
// destination file.  On success, return 0.  On failure, abort
// ============================================================================
i32 fsWrite(i32 fd, i32 numb, void *buf)
{
    printf("== ENTERING FSWRITE() ==\n");

    i32 inum = bfsFdToInum(fd);
    Inode inode;
    bfsReadInode(inum, &inode);

    i32 cursor = bfsTell(fd);
    i32 startFBN = cursor / BYTESPERBLOCK;
    i32 offset = cursor % BYTESPERBLOCK;
    i32 numBlocks = (numb / BYTESPERBLOCK) + 1;

    if (ENODBN == 0)
        abort();

    i32 leftToWrite = numb;
    i32 numToWrite = leftToWrite;

    for (int i = 0; i < numBlocks; i++)
    {
        if (leftToWrite > BYTESPERBLOCK)
        {
            numToWrite = BYTESPERBLOCK - offset;
        }

        writeToBlock(inum, offset, startFBN, numToWrite, buf);

        bfsSetCursor(inum, cursor + numToWrite);
        cursor = bfsTell(fd);

        leftToWrite -= numToWrite;
        numToWrite = leftToWrite;

        startFBN++;
        offset = 0; // continuing to write onto the next block
    }

    printf("=== EXITTING FSWRITE() ===\n");
    return 0;
}