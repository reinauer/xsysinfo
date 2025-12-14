/*
 * xSysInfo - Memory information and view
 */

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include <exec/execbase.h>
#include <exec/memory.h>

#include <proto/exec.h>
#include <proto/graphics.h>

#include "xsysinfo.h"
#include "memory.h"
#include "gui.h"
#include "locale_str.h"
#include "benchmark.h"

/* Global memory region list */
MemoryRegionList memory_regions;

/* External references */
extern struct ExecBase *SysBase;
extern AppContext *app;

/*
 * Get human-readable memory type string
 */
const char *get_memory_type_string(UWORD attrs, APTR addr)
{
    static char buffer[64];
    ULONG address = (ULONG)addr;

    buffer[0] = '\0';

    /* Check for specific memory regions */
    if (attrs & MEMF_CHIP) {
        strcat(buffer, "CHIP RAM");
    } else if (address >= 0xC00000 && address < 0xD80000) {
        /* Ranger/Slow RAM area */
        strcat(buffer, "SLOW RAM");
    } else if (attrs & MEMF_FAST) {
        if (address < 0x01000000) {
            strcat(buffer, "FAST RAM (24bit)");
        } else {
            strcat(buffer, "FAST RAM (32bit)");
        }
    } else {
        strcat(buffer, "RAM");
    }

    if (attrs & MEMF_LOCAL) {
        strcat(buffer, ", LOCAL");
    }

    if (attrs & MEMF_PUBLIC) {
        strcat(buffer, ", PUBLIC");
    }
    if (attrs & MEMF_KICK) {
        strcat(buffer, ", KICK");
    }
    if (attrs & MEMF_24BITDMA) {
        strcat(buffer, ", 24BitDMA");
    }

    return buffer;
}

/*
 * Analyze memory region - count chunks and find largest block
 */
void analyze_memory_region(struct MemHeader *mh, ULONG *chunks, ULONG *largest)
{
    struct MemChunk *mc;
    ULONG count = 0;
    ULONG max_size = 0;

    *chunks = 0;
    *largest = 0;

    if (!mh) return;

    /* Walk the free list */
    for (mc = mh->mh_First; mc != NULL; mc = mc->mc_Next) {
        count++;
        if (mc->mc_Bytes > max_size) {
            max_size = mc->mc_Bytes;
        }
    }

    *chunks = count;
    *largest = max_size;
}

/*
 * Enumerate all memory regions
 */
void enumerate_memory_regions(void)
{
    struct MemHeader *mh;

    memset(&memory_regions, 0, sizeof(memory_regions));

    Forbid();

    for (mh = (struct MemHeader *)SysBase->MemList.lh_Head;
         (struct Node *)mh != (struct Node *)&SysBase->MemList.lh_Tail;
         mh = (struct MemHeader *)mh->mh_Node.ln_Succ) {

        if (memory_regions.count >= MAX_MEMORY_REGIONS) break;

        MemoryRegion *region = &memory_regions.regions[memory_regions.count];

        region->start_address = (APTR)((ULONG)mh->mh_Lower & 0xffff8000);
        region->end_address = mh->mh_Upper - 1;
        region->total_size = (ULONG)(mh->mh_Upper - region->start_address);
        region->mem_type = mh->mh_Attributes;
        region->priority = mh->mh_Node.ln_Pri;
        region->lower_bound = mh->mh_Lower;
        region->upper_bound = mh->mh_Upper;
        region->first_free = mh->mh_First;
        region->amount_free = mh->mh_Free;

        analyze_memory_region(mh, &region->num_chunks, &region->largest_block);

        if (mh->mh_Node.ln_Name) {
            strncpy(region->node_name, mh->mh_Node.ln_Name,
                    sizeof(region->node_name) - 1);
        } else {
            strncpy(region->node_name, "(unnamed)",
                    sizeof(region->node_name) - 1);
        }

        strncpy(region->type_string,
                get_memory_type_string(mh->mh_Attributes, mh->mh_Lower),
                sizeof(region->type_string) - 1);

        memory_regions.count++;
    }

    Permit();
}

/*
 * Refresh a single memory region (for updated free memory info)
 */
void refresh_memory_region(ULONG index)
{
    struct MemHeader *mh;
    ULONG i = 0;

    if (index >= memory_regions.count) return;

    Forbid();

    for (mh = (struct MemHeader *)SysBase->MemList.lh_Head;
         (struct Node *)mh != (struct Node *)&SysBase->MemList.lh_Tail;
         mh = (struct MemHeader *)mh->mh_Node.ln_Succ) {

        if (i == index) {
            MemoryRegion *region = &memory_regions.regions[index];
            region->first_free = mh->mh_First;
            region->amount_free = mh->mh_Free;
            analyze_memory_region(mh, &region->num_chunks, &region->largest_block);
            break;
        }
        i++;
    }

    Permit();
}

/*
 * Measure memory read speed for a region
 * Returns bytes per second
 */
ULONG measure_memory_speed(ULONG index)
{
    MemoryRegion *region;
    ULONG buffer_size;
    ULONG num_reads;
    ULONG total_read = 0;
    uint64_t start_time, end_time, elapsed;
    ULONG bytes_per_sec = 0;
    volatile ULONG *src;
    volatile ULONG dummy;
    ULONG i, j;
    ULONG longs_per_read;

    if (index >= memory_regions.count) return 0;

    region = &memory_regions.regions[index];

    /* Use a reasonable buffer size - 64K for test reads */
    buffer_size = 64 * 1024;

    /* Limit to available region size */
    if (buffer_size > region->total_size) {
        buffer_size = region->total_size;
    }

    /* Ensure we're reading aligned longwords */
    buffer_size &= ~3;
    if (buffer_size < 256) {
        /* Region too small for meaningful test */
        region->speed_measured = TRUE;
        region->speed_bytes_sec = 0;
        return 0;
    }

    /* Number of read passes to get a meaningful measurement */
    num_reads = 16;

    /* Calculate longs per read pass */
    longs_per_read = buffer_size / sizeof(ULONG);

    /* Get start time */
    start_time = get_timer_ticks();

    /* Perform read test directly on the memory region */
    src = (volatile ULONG *)region->start_address;

    for (i = 0; i < num_reads; i++) {
        /* Read through the buffer */
        for (j = 0; j < longs_per_read; j++) {
            dummy = src[j];
        }
        total_read += buffer_size;
    }

    /* Suppress unused variable warning */
    (void)dummy;

    /* Get end time */
    end_time = get_timer_ticks();

    /* Calculate speed */
    elapsed = end_time - start_time;
    if (elapsed > 0 && total_read > 0) {
        /* Timer ticks are in microseconds */
        bytes_per_sec = (ULONG)(((uint64_t)total_read * 1000000ULL) / elapsed);
    }

    region->speed_bytes_sec = bytes_per_sec;
    region->speed_measured = TRUE;

    return bytes_per_sec;
}

/*
 * Draw memory view
 */
void draw_memory_view(void)
{
    struct RastPort *rp = app->rp;
    char buffer[64];
    WORD y;
    MemoryRegion *region;

    /* Draw title panel */
    draw_panel(100, 0, 520, 24, NULL);

    SetAPen(rp, COLOR_TEXT);
    SetBPen(rp, COLOR_PANEL_BG);
    Move(rp, 250, 14);
    Text(rp, (CONST_STRPTR)get_string(MSG_MEMORY_INFO), strlen(get_string(MSG_MEMORY_INFO)));

    if (memory_regions.count == 0) {
        Move(rp, 200, 120);
        Text(rp, (CONST_STRPTR)"No memory regions found", 23);
        return;
    }

    /* Draw memory info panel */
    draw_panel(100, 28, 520, 150, NULL);

    /* Refresh current region data */
    refresh_memory_region(app->memory_region_index);
    region = &memory_regions.regions[app->memory_region_index];

    /* Draw memory info */
    y = 44;

    /* Start address */
    snprintf(buffer, sizeof(buffer), "$%08lX", (unsigned long)region->start_address);
    draw_label_value(128, y, get_string(MSG_START_ADDRESS), buffer, 168);
    y += 10;

    /* End address */
    snprintf(buffer, sizeof(buffer), "$%08lX", (unsigned long)region->end_address);
    draw_label_value(128, y, get_string(MSG_END_ADDRESS), buffer, 168);
    y += 10;

    /* Total size */
    format_size(region->total_size, buffer, sizeof(buffer));
    draw_label_value(128, y, get_string(MSG_TOTAL_SIZE), buffer, 168);
    y += 10;

    /* Memory type */
    draw_label_value(128, y, get_string(MSG_MEMORY_TYPE), region->type_string, 168);
    y += 10;

    /* Priority */
    snprintf(buffer, sizeof(buffer), "%d", region->priority);
    draw_label_value(128, y, get_string(MSG_PRIORITY), buffer, 168);
    y += 10;

    /* Lower bound */
    snprintf(buffer, sizeof(buffer), "$%08lX", (unsigned long)region->lower_bound);
    draw_label_value(128, y, get_string(MSG_LOWER_BOUND), buffer, 168);
    y += 10;

    /* Upper bound */
    snprintf(buffer, sizeof(buffer), "$%08lX", (unsigned long)region->upper_bound);
    draw_label_value(128, y, get_string(MSG_UPPER_BOUND), buffer, 168);
    y += 10;

    /* First free address */
    snprintf(buffer, sizeof(buffer), "$%08lX", (unsigned long)region->first_free);
    draw_label_value(128, y, get_string(MSG_FIRST_ADDRESS), buffer, 168);
    y += 10;

    /* Amount free */
    snprintf(buffer, sizeof(buffer), "%lu Bytes", (unsigned long)region->amount_free);
    draw_label_value(128, y, get_string(MSG_AMOUNT_FREE), buffer, 168);
    y += 10;

    /* Largest block */
    snprintf(buffer, sizeof(buffer), "%lu Bytes", (unsigned long)region->largest_block);
    draw_label_value(128, y, get_string(MSG_LARGEST_BLOCK), buffer, 168);
    y += 10;

    /* Number of chunks */
    snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)region->num_chunks);
    draw_label_value(128, y, get_string(MSG_NUM_CHUNKS), buffer, 168);
    y += 10;

    /* Node name */
    draw_label_value(128, y, get_string(MSG_NODE_NAME), region->node_name, 168);
    y += 10;

    /* Memory speed - display in appropriate units */
    if (region->speed_measured) {
        ULONG speed = region->speed_bytes_sec;
        if (speed >= 1000000) {
            /* MB/s for fast memory */
            snprintf(buffer, sizeof(buffer), "%lu.%lu MB/s",
                     (unsigned long)(speed / 1000000),
                     (unsigned long)((speed % 1000000) / 100000));
        } else if (speed >= 10000) {
            /* KB/s */
            snprintf(buffer, sizeof(buffer), "%lu.%lu KB/s",
                     (unsigned long)(speed / 1000),
                     (unsigned long)((speed % 1000) / 100));
        } else if (speed > 0) {
            /* Bytes/s for very slow memory */
            snprintf(buffer, sizeof(buffer), "%lu B/s", (unsigned long)speed);
        } else {
            strncpy(buffer, "---", sizeof(buffer));
        }
    } else {
        strncpy(buffer, "---", sizeof(buffer));
    }
    draw_label_value(128, y, get_string(MSG_MEMORY_SPEED), buffer, 168);

    /* Draw navigation buttons */
    Button *btn;
    btn = find_button(BTN_MEM_PREV);
    if (btn) draw_button(btn);
    btn = find_button(BTN_MEM_COUNTER);
    if (btn) draw_button(btn);
    btn = find_button(BTN_MEM_NEXT);
    if (btn) draw_button(btn);
    btn = find_button(BTN_MEM_SPEED);
    if (btn) draw_button(btn);
    btn = find_button(BTN_MEM_EXIT);
    if (btn) draw_button(btn);
}
