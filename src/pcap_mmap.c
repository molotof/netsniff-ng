/*
 * netsniff-ng - the packet sniffing beast
 * By Daniel Borkmann <daniel@netsniff-ng.org>
 * Copyright 2011 Daniel Borkmann.
 * Subject to the GPL, version 2.
 */

#define _GNU_SOURCE 
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "pcap.h"
#include "xio.h"
#include "opt_memcpy.h"
#include "locking.h"

#define DEFAULT_SLOTS     1000

#define PAGE_SIZE	  (getpagesize())
#define PAGE_MASK	  (~(PAGE_SIZE - 1))
#define PAGE_ALIGN(addr)  (((addr) + PAGE_SIZE - 1) & PAGE_MASK)

static struct spinlock lock;
static off_t map_size = 0;
static char *pstart, *pcurr;
static int jumbo_frames = 0;

static inline off_t get_map_size(void)
{
	int allocsz = jumbo_frames ? 16 : 3;
	return PAGE_ALIGN(sizeof(struct pcap_filehdr) +
			  (PAGE_SIZE * allocsz) * DEFAULT_SLOTS);
}

static int pcap_mmap_pull_file_header(int fd)
{
	ssize_t ret;
	struct pcap_filehdr hdr;

	ret = read(fd, &hdr, sizeof(hdr));
	if (unlikely(ret != sizeof(hdr)))
		return -EIO;
	pcap_validate_header_maybe_die(&hdr);

	return 0;
}

static int pcap_mmap_push_file_header(int fd)
{
	ssize_t ret;
	struct pcap_filehdr hdr;

	memset(&hdr, 0, sizeof(hdr));
	pcap_prepare_header(&hdr, LINKTYPE_EN10MB, 0,
			    PCAP_DEFAULT_SNAPSHOT_LEN);
	ret = write_or_die(fd, &hdr, sizeof(hdr));
	if (unlikely(ret != sizeof(hdr))) {
		whine("Failed to write pkt file header!\n");
		return -EIO;
	}

	return 0;
}

static int pcap_mmap_prepare_writing_pcap(int fd)
{
	int ret;
	struct stat sb;

	spinlock_lock(&lock);
	map_size = get_map_size();
	ret = fstat(fd, &sb);
	if (ret < 0)
		panic("Cannot fstat pcap file!\n");
	if (!S_ISREG (sb.st_mode))
		panic("pcap dump file is not a regular file!\n");
	/* Expand file buffer, so that mmap can be done. */
	ret = lseek(fd, map_size, SEEK_SET);
	if (ret < 0)
		panic("Cannot lseek pcap file!\n");
	ret = write_or_die(fd, "", 1);
	if (ret != 1)
		panic("Cannot write file!\n");
	pstart = mmap(0, map_size, PROT_WRITE, MAP_SHARED
		      /*| MAP_HUGETLB*/, fd, 0);
	if (pstart == MAP_FAILED)
		panic("mmap of file failed!");
	ret = madvise(pstart, map_size, MADV_SEQUENTIAL);
	if (ret < 0)
		panic("Failed to give kernel mmap advise!\n");
	pcurr = pstart + sizeof(struct pcap_filehdr);
	spinlock_unlock(&lock);

	return 0;
}

static ssize_t pcap_mmap_write_pcap_pkt(int fd, struct pcap_pkthdr *hdr,
					uint8_t *packet, size_t len)
{
	int ret;

	spinlock_lock(&lock);
	if ((off_t) (pcurr - pstart) + sizeof(*hdr) + len > map_size) {
		off_t map_size_old = map_size;
		off_t offset = (pcurr - pstart);
		map_size = PAGE_ALIGN(map_size_old * 3 / 2);
		ret = lseek(fd, map_size, SEEK_SET);
		if (ret < 0)
			panic("Cannot lseek pcap file!\n");
		ret = write_or_die(fd, "", 1);
		if (ret != 1)
			panic("Cannot write file!\n");
		pstart = mremap(pstart, map_size_old, map_size, MREMAP_MAYMOVE);
		if (pstart == MAP_FAILED)
			panic("mmap of file failed!");
		ret = madvise(pstart, map_size, MADV_SEQUENTIAL);
		if (ret < 0)
			panic("Failed to give kernel mmap advise!\n");
		pcurr = pstart + offset;
	}
	__memcpy_small(pcurr, hdr, sizeof(*hdr));
	pcurr += sizeof(*hdr);
	__memcpy(pcurr, packet, len);
	pcurr += len;
	spinlock_unlock(&lock);

	return sizeof(*hdr) + len;
}

static int pcap_mmap_prepare_reading_pcap(int fd)
{
	int ret;
	struct stat sb;

	spinlock_lock(&lock);
	ret = fstat(fd, &sb);
	if (ret < 0)
		panic("Cannot fstat pcap file!\n");
	if (!S_ISREG (sb.st_mode))
		panic("pcap dump file is not a regular file!\n");
	map_size = sb.st_size;
	pstart = mmap(0, map_size, PROT_READ, MAP_SHARED | MAP_LOCKED
		      /*| MAP_HUGETLB*/, fd, 0);
	if (pstart == MAP_FAILED)
		panic("mmap of file failed!");
	ret = madvise(pstart, map_size, MADV_SEQUENTIAL);
	if (ret < 0)
		panic("Failed to give kernel mmap advise!\n");
	pcurr = pstart + sizeof(struct pcap_filehdr);
	spinlock_unlock(&lock);

	return 0;
}

static ssize_t pcap_mmap_read_pcap_pkt(int fd, struct pcap_pkthdr *hdr,
				       uint8_t *packet, size_t len)
{
	spinlock_lock(&lock);
	if (unlikely((off_t) (pcurr + sizeof(*hdr) - pstart) > map_size)) {
		spinlock_unlock(&lock);
		return -ENOMEM;
	}
	__memcpy_small(hdr, pcurr, sizeof(*hdr));
	pcurr += sizeof(*hdr);
	if (unlikely((off_t) (pcurr + hdr->len - pstart) > map_size)) {
		spinlock_unlock(&lock);
		return -ENOMEM;
	}
	__memcpy(packet, pcurr, hdr->len);
	pcurr += hdr->len;
	spinlock_unlock(&lock);

	if (unlikely(hdr->len == 0))
		return -EINVAL; /* Bogus packet */
	return sizeof(*hdr) + hdr->len;
}

static void pcap_mmap_fsync_pcap(int fd)
{
	spinlock_lock(&lock);
	msync(pstart, (off_t) (pcurr - pstart), MS_ASYNC);
	spinlock_unlock(&lock);
}

static void pcap_mmap_prepare_close_pcap(int fd, enum pcap_mode mode)
{
	spinlock_lock(&lock);
	int ret = munmap(pstart, map_size);
	if (ret < 0)
		panic("Cannot unmap the pcap file!\n");
	if (mode == PCAP_MODE_WRITE) {
		ret = ftruncate(fd, (off_t) (pcurr - pstart));
		if (ret)
			panic("Cannot truncate the pcap file!\n");
	}
	spinlock_unlock(&lock);
}

struct pcap_file_ops pcap_mmap_ops __read_mostly = {
	.name = "MMAP",
	.pull_file_header = pcap_mmap_pull_file_header,
	.push_file_header = pcap_mmap_push_file_header,
	.prepare_writing_pcap = pcap_mmap_prepare_writing_pcap,
	.write_pcap_pkt = pcap_mmap_write_pcap_pkt,
	.prepare_reading_pcap = pcap_mmap_prepare_reading_pcap,
	.read_pcap_pkt = pcap_mmap_read_pcap_pkt,
	.fsync_pcap = pcap_mmap_fsync_pcap,
	.prepare_close_pcap = pcap_mmap_prepare_close_pcap,
};

int init_pcap_mmap(int jumbo_support)
{
	spinlock_init(&lock);
	jumbo_frames = jumbo_support;
	return pcap_ops_group_register(&pcap_mmap_ops, PCAP_OPS_MMAP);
}

void cleanup_pcap_mmap(void)
{
	spinlock_destroy(&lock);
	pcap_ops_group_unregister(PCAP_OPS_MMAP);
}

