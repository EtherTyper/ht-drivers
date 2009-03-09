/**
 * @file cdcmBoth.h
 *
 * @brief Contains definitions that are used both, by LynxOS and Linux drivers.
 *
 * @author Georgievskiy Yury, Alain Gagnaire. CERN AB/CO.
 *
 * @date Created on 21/07/2006
 *
 * It should be included in the user part of the driver.
 * Many thanks to Julian Lewis and Nicolas de Metz-Noblat.
 *
 * @version
 */
#ifndef _CDCM_BOTH_H_INCLUDE_
#define _CDCM_BOTH_H_INCLUDE_

#ifdef __linux__

#include <asm/scatterlist.h>
#include <asm/types.h>
#include <linux/jiffies.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/scatterlist.h>
#define cdcm_dma_t dma_addr_t
#define CDCM_IRQ_HANDLED IRQ_HANDLED

#else  /* __Lynx__ */

#include <sys/types.h>
#include <mem.h>
#include <conf.h>
#define cdcm_dma_t unsigned char* /* Valid for Power PC */
#define CDCM_IRQ_HANDLED

#endif /* !__linux__ */




#ifdef __linux__

#if defined(__LITTLE_ENDIAN) && defined(__BIG_ENDIAN)
# error CDCM: Fix asm/byteorder.h to define one endianness.
#endif

#if !defined(__LITTLE_ENDIAN) && !defined(__BIG_ENDIAN)
# error CDCM: Fix asm/byteorder.h to define one endianness.
#endif

#if defined(__LITTLE_ENDIAN)
#define CDCM_LITTLE_ENDIAN 1234
#else /* __BIG_ENDIAN */
#define CDCM_BIG_ENDIAN 4321
#endif

#else /* LynxOS */

#if defined(__LITTLE_ENDIAN__) && defined(__BIG_ENDIAN__)
# error CDCM: Conflicting endianities defined in this machine.
#endif

#if !defined(__LITTLE_ENDIAN__) && !defined(__BIG_ENDIAN__)
# error CDCM: Conflicting endianities defined in this machine.
#endif

#if defined(__LITTLE_ENDIAN__)
#define CDCM_LITTLE_ENDIAN 1234
#else /* __BIG_ENDIAN__ */
#define CDCM_BIG_ENDIAN 4321
#endif

#endif /* __linux__ */



#ifndef cdcm_irqret_t
#ifdef __linux__
#define cdcm_irqret_t irqreturn_t
#else
#define cdcm_irqret_t void
#endif
#endif


/* ms_to_ticks, ticks_to_ms conversion */

/* Pay attention to precedence in these operations!
 * if you divide first, you'll always return 0.
 * Therefore the  macros below will return 0 for sub-tick delays.
 */
#ifdef __linux__
#define cdcm_ticks_to_ms(x) ((x) * 1000 / HZ)
#define cdcm_ms_to_ticks(x) ((x) * HZ / 1000)
#else /* Lynx */
#define cdcm_ticks_to_ms(x) ((x) * 1000 / TICKSPERSEC)
#define cdcm_ms_to_ticks(x) ((x) * TICKSPERSEC / 1000)
#endif /* !__linux__ */


/* DMA interfaces */
#ifdef __linux__
/*! @name cdcm_dmachain
 * Assimilates in a portable way LynxOS' struct dmachain.
 * address is the (portable) address to be used,
 * and count is the page size.
 */
//@{
struct dmachain {
  cdcm_dma_t address;
  unsigned short count;
};//@}
#endif

struct cdcm_dmabuf {
#ifdef __linux__
  /* for userland buffer */
  int                 offset;
  int                 tail;
  struct page         **pages;

  /* for kernel buffers */
  void                *vmalloc;

  /* for overlay buffers (pci-pci dma) */
  cdcm_dma_t          bus_addr;

  /* common */
  struct scatterlist  *sglist;
  int                 sglen;
  int                 nr_pages;
  int                 direction;

#else
  char *user_buf;
  unsigned long size;
#endif
};


int cdcm_copy_from_user(void *, void *, int);
int cdcm_copy_to_user(void *, void *, int);

cdcm_dma_t cdcm_pci_map(void *, void *, size_t, int);
void cdcm_pci_unmap(void *, cdcm_dma_t, int, int);
int cdcm_pci_mmchain_lock(void *, struct cdcm_dmabuf *, int , int, void *,
			  unsigned long, struct dmachain *);
int cdcm_pci_mem_unlock(void *, struct cdcm_dmabuf *, int, int);
int __cdcm_clear_dma(struct cdcm_dmabuf *, int);

#ifdef __Lynx__

#define CDCM_LOOP_AGAIN

#endif

#endif /* _CDCM_BOTH_H_INCLUDE_ */
