CPA
***

.. Describe the importance of your project, and what it does.

This CPA project provides an integration guide and tests for the Compound Page
Allocator (CPA). CPA is an ION heap which allocates fixed-size *large pages*
(> 4kB) from the system, which might not otherwise be guaranteed. Certain
use-cases require larger contiguous regions of physical memory when mapped into
a device IOMMU. For example, high resolution buffers require a greater number
of page tables which can impact display hardware performance, especially when
rotated. Also, memory might be protected at a fixed (and often large) physical
address granule. Large pages can be allocated from a carveout but this region
is exclusively reserved at boot-time and unavailable to the system. The
Contiguous Memory Allocator (CMA) is a slight improvement but might incur
delay while migrating pages back from the system.

CPA holds a configurable, pre-allocated *large page* pool to reduce the overhead
of memory allocations. There are three water-marks (low/high and fill) and one
asynchronous kernel thread to fill/drain the page pool. When the pool hits its
low-mark, the asynchronous thread is triggered to start filling the page pool
until it reaches the fill-mark level. When the page pool hits its the high-mark
(due to allocations being freed), freed pages will be returned to the system
instead of CPAs page pool. A memory shrink interface is also implemented in
CPA. This interface is used when the system is running in a low memory
situation, at which time pages in the pool are released back to the system,
unlike the carveout heap.



.. Add blank line before section header
|

CPA Integration
===============

CPA must be integrated in both the user-space and kernel-space. In order to
allocate memory through CPA (as an ION heap), some platform specific integration
is required within the ION driver. To enable use of CPA from an Android Gralloc
HAL, the correct heap mask must be defined in user-space and match the kernel
value. More information will be provided in the steps below.



Integration with ION (3.18 Linux Kernel)
----------------------------------------

The generic integration steps are as follows:


#. Obtain 3.18 kernel with ION and latest version of `ion_compound_page.c
   <https://git.linaro.org/landing-teams/working/arm/kernel.git/tree/drivers/
   staging/android/ion/ion_compound_page.c?h=arm-juno-mali-fpga&
   id=0a5304e1d0afcc20abe00f92631f889afc5800ae>`_ (based on **3.18 kernel**):


   .. code-block:: bash

      # Checkout 3.18 kernel from Linus' repo
      git clone https://github.com/torvalds/linux.git
      cd linux
      git checkout v3.18

      # Copy ion_compound_page.c into ION
      cp ion_compound_page.c drivers/staging/android/ion/



#. Patch ION to fix issue with Juno DMA heap (`commit <https://git.linaro.org/
   landing-teams/working/arm/kernel.git/commit/drivers/staging/android/ion/ion.c
   ?h=arm-juno-mali-fpga&id=ecba8723122fc38cf9fbce820f469a6508fd6b55>`_):

   .. code-block:: none

      patch --ignore-whitespace -p1 < ion.patch

   .. code-block:: none

      --- a/drivers/staging/android/ion/ion.c
      +++ b/drivers/staging/android/ion/ion.c
      @@ -252,8 +252,19 @@ static struct ion_buffer *ion_buffer_create(struct ion_heap *heap,
                 allocation via dma_map_sg. The implicit contract here is that
                 memory comming from the heaps is ready for dma, ie if it has a
                 cached mapping that mapping has been invalidated */
      -       for_each_sg(buffer->sg_table->sgl, sg, buffer->sg_table->nents, i)
      -               sg_dma_address(sg) = sg_phys(sg);
      +        for_each_sg(buffer->sg_table->sgl, sg, buffer->sg_table->nents, i) {
      +                if(buffer && buffer->heap && buffer->heap->ops && buffer->heap->ops->phys) {
      +                        ion_phys_addr_t addr;
      +                        size_t len;
      +                        buffer->heap->ops->phys(buffer->heap, buffer, &addr, &len);
      +                        sg_dma_address(sg) = addr;
      +                        sg_dma_len(sg) = len;
      +                } else {
      +                        sg_dma_address(sg) = sg_phys(sg);
      +                        sg_dma_len(sg) = sg->length;
      +                }
      +        }
      +
              mutex_lock(&dev->buffer_lock);
              ion_buffer_add(dev, buffer);
              mutex_unlock(&dev->buffer_lock);

   ..

     NOTE: This is the minimum patch set required to run the tests, however
     other ION fixes from `Linaro Juno 3.18 kernel (ION)
     <https://git.linaro.org/landing-teams/working/arm/kernel.git/tree/drivers/
     staging/android/ion?h=arm-juno-mali-fpga>`_ might be desirable.





#. Integrate CPA in ION:

   a. Add compound page configuration options to `ion/Kconfig
      <https://git.linaro.org/landing-teams/working/arm/kernel.git/tree/drivers/
      staging/android/ion/Kconfig?h=arm-juno-mali-fpga&
      id=618d9cb1670bdf465fe73faa1b0d3d5eb7fd2843>`_ (see
      ``ION_COMPOUND_PAGE`` and ``ION_COMPOUND_PAGE_STATS``):

      .. code-block:: none

            config ION_COMPOUND_PAGE
                  bool "Ion compound page system pool"
                  depends on ION
                  help
                  Enable use of compound pages (default to 2MB) system memory pool.
                  Backs a buffer with large and aligned pages where possible,
                  to ease TLB pressure and lessen memory fragmentation.


            config ION_COMPOUND_PAGE_STATS
                  bool "Collect statistics for the compound page pool"
                  depends on ION_COMPOUND_PAGE && DEBUG_FS
                  help
                  Collect extra usage statistics for the compound page pool.
                  Available via ion's debugfs entry for the pool.


   b. Add compound page allocator to `ion/Makefile
      <https://git.linaro.org/landing-teams/working/arm/kernel.git/tree/drivers/
      staging/android/ion/Makefile?h=arm-juno-mali-fpga&
      id=2b8cff766e6857b26603dfecd3b562ac1c8b60f5>`_:

      .. code-block:: none

         obj-$(CONFIG_ION_COMPOUND_PAGE) += ion_compound_page.o


   c. Add ``ION_HEAP_TYPE_COMPOUND_PAGE`` to ``ion_heap_type`` enum (after
      ``ION_HEAP_TYPE_DMA``) in ION uapi `drivers/staging/android/uapi/ion.h
      <https://git.linaro.org/landing-teams/working/arm/kernel.git/tree/drivers/
      staging/android/uapi/ion.h?h=arm-juno-mali-fpga&
      id=0a5304e1d0afcc20abe00f92631f889afc5800ae>`_


   d. Expose compound page heap through ION core as per `example
      <https://git.linaro.org/landing-teams/working/arm/kernel.git/commit/
      drivers/staging/android?h=arm-juno-mali-fpga&
      id=cbf003119fa4785dea78c708e1b2bde2823491e7>`_:

      - Add following code to `ion/ion_heap.c
        <https://git.linaro.org/landing-teams/working/arm/kernel.git/tree/
        drivers/staging/android/ion/ion_heap.c?h=arm-juno-mali-fpga&
        id=0a5304e1d0afcc20abe00f92631f889afc5800ae>`_, ``ion_heap_create`` and
        ``ion_heap_destroy`` respectively:

        .. code-block:: c

            case ION_HEAP_TYPE_COMPOUND_PAGE:
              heap = ion_compound_page_pool_create(heap_data);
              break;

        .. code-block:: c

            case ION_HEAP_TYPE_COMPOUND_PAGE:
              ion_compound_page_pool_destroy(heap);
              break;

      - Add following code to `ion/ion_priv.h
        <https://git.linaro.org/landing-teams/working/arm/kernel.git/tree/
        drivers/staging/android/ion/ion_priv.h?h=arm-juno-mali-fpga&
        id=0a5304e1d0afcc20abe00f92631f889afc5800ae>`_:

        .. code-block:: c

            struct ion_heap *ion_compound_page_pool_create(struct ion_platform_heap *);
            void ion_compound_page_pool_destroy(struct ion_heap *);


   e. Declare compound page platform data ``ion_cpa_platform_data`` in
      `ion/ion.h <https://git.linaro.org/landing-teams/working/arm/kernel.git/
      tree/drivers/staging/android/ion/ion.h?h=arm-juno-mali-fpga&
      id=0a5304e1d0afcc20abe00f92631f889afc5800ae>`_:

      .. code-block:: c

          /**
           * struct ion_cpa_platform_data - settings for a cpa heap instance
           * @lowmark:    Lowest number of items on free list before refill is
           *      triggered
           * @highmark:   Maximum number of item on free list
           * @fillmark:   Number of items to target during a refill
           * @align_order:  Order to round-up allocation sizes to
           * @order:    Order of the compound pages to break allocations into
           *
           * Provided as the priv data for a cpa heap
           */
          struct ion_cpa_platform_data {
            int lowmark;
            int highmark;
            int fillmark;
            int align_order;
            int order;
          };



#. Add compound page heap and platform data to ION device. There is no need
   to create a new device if dummy is already being used. See Juno example
   here: `ion/juno/juno_ion_dev.c
   <https://git.linaro.org/landing-teams/working/arm/kernel.git/tree/drivers/
   staging/android/ion/juno/juno_ion_dev.c?h=arm-juno-mali-fpga&
   id=0a5304e1d0afcc20abe00f92631f889afc5800ae>`_:

   a. Add ION platform if one does not exist. This comprises a directory
      containing the following files:

      .. code-block:: none

          <platform>/
          ├── <platform>_ion_dev.c
          ├── <platform>_ion_driver.c
          ├── Makefile

      ..

          Juno example can be found `here <https://git.linaro.org/landing-teams/
          working/arm/kernel.git/tree/drivers/staging/android/ion/juno?
          h=arm-juno-mali-fpga&id=0a5304e1d0afcc20abe00f92631f889afc5800ae>`_


   b. Define the compound page ``ion_platform_heap`` as follows:

      .. code-block:: c

         {
             .id = ION_HEAP_TYPE_COMPOUND_PAGE,
             .type = ION_HEAP_TYPE_COMPOUND_PAGE,
             .name = "compound_page",
             .priv = &cpa_config,
         }

      ..

        NOTE: ensure that the number of heaps ``nr`` in ``ion_platform_data``
        is also updated.

   c. Define compound page platform data ``ion_cpa_platform_data`` as follows:

      .. code-block:: c

         static struct ion_cpa_platform_data cpa_config = {
             .lowmark = 8,
             .highmark = 128,
             .fillmark = 64,
             .align_order = 0,
             .order = 9,
         };

      ..

        NOTE: These values should be tuned for the platform and might cause the
        system to run into low memory condition if the pool is set too large.
        low/fill/high marks and allocation/alignment page order can be
        modified as per ``cpa_platform_data`` declaration `here
        <https://git.linaro.org/landing-teams/working/arm/kernel.git/tree/
        drivers/staging/android/ion/ion.h?h=arm-juno-mali-fpga&
        id=0a5304e1d0afcc20abe00f92631f889afc5800ae#n73>`_




#. Enable ION and CPA in the kernel configuration:

   a. Add following to *.conf (e.g. *android.conf*):

      .. code-block:: none

         CONFIG_ION=y
         CONFIG_ION_<PLATFORM>=y
         CONFIG_ION_COMPOUND_PAGE=y
         CONFIG_ION_COMPOUND_PAGE_STATS=y

      where ``<PLATFORM>`` is the name of the platform (e.g.
      ``CONFIG_ION_JUNO``).

        NOTE: ``CONFIG_ION_COMPOUND_PAGE_STATS`` is only required for statistics
        collection (useful when looking at CPA behaviour)


   b. Add configuration option to enable ION platform in `ion/Kconfig
      <https://git.linaro.org/landing-teams/working/arm/kernel.git/tree/drivers/
      staging/android/ion/Kconfig?h=arm-juno-mali-fpga>`_:

      .. code-block:: none

          config ION_<PLATFORM>
              bool "Ion for <PLATFORM>"
              depends on ION
              help
              ION support for <PLATFORM>.

      where, for example, ``<PLATFORM>`` is ``JUNO``


   c. Add platform directory/files to `ion/Makefile <https://git.linaro.org/
      landing-teams/working/arm/kernel.git/tree/drivers/staging/android/ion/
      Makefile?h=arm-juno-mali-fpga>`_:

      .. code-block:: none

          obj-$(CONFIG_ION_<PLATFORM>) += <platform>/

      where, for example, ``<PLATFORM>`` is ``JUNO`` and ``<platform>`` is
      ``juno``



Integration with Gralloc (Android userspace)
--------------------------------------------

1. Update `external/kernel-headers/original/uapi/linux/ion.h
   <http://androidxref.com/8.0.0_r4/xref/external/kernel-headers/original/uapi/
   linux/ion.h>`_ in Android tree with compound page heap info:

   a. Add ``ION_HEAP_TYPE_COMPOUND_PAGE`` to ``ion_heap_type``:

        NOTE: the order of heaps in ``ion_heap_type`` must match the kernel
        version of header *drivers/staging/android/uapi/ion.h*

   b. Add compound page mask:

      .. code-block:: c

          #define ION_HEAP_TYPE_COMPOUND_PAGE_MASK (1 << ION_HEAP_TYPE_COMPOUND_PAGE)



2. Run `update\_all.py <http://androidxref.com/8.0.0_r4/xref/bionic/libc/
   kernel/tools/update_all.py>`_ to re-generate the bionic uapi
   `bionic/libc/kernel/uapi/linux/ion.h <http://androidxref.com/
   8.0.0_r4/xref/bionic/libc/kernel/uapi/linux/ion.h>`_

     NOTE: if libclang-3.5.so can't be found by the script, try linking to
     the default (un-versioned):

     ..

     .. code-block:: none

        ln -s <path-to-android-tree>/prebuilts/sdk/tools/linux/lib64/libclang.so <path-to-android-tree>/prebuilts/sdk/tools/linux/lib64/libclang-3.5.so

   ..

     NOTE: check that the changes made in ion.h header are present in the
     newly generated header:
     *bionic/libc/kernel/uapi/linux/ion.h*


3. Copy the newly generated bionic ION uapi header to libion:

   .. code-block:: none

      cp <path-to-android-tree>/bionic/libc/kernel/uapi/linux/ion.h <path-to-android-tree>/system/core/libion/kernel-headers/linux/ion.h


4. Use the ION compound page heap mask for all CPA allocations:

   .. code-block:: c

      ////// file: test.c

      #include <linux/ion.h>

      #if defined(ION_HEAP_TYPE_COMPOUND_PAGE_MASK)
          ret = ion_alloc(ion_client, size, 0, ION_HEAP_TYPE_COMPOUND_PAGE_MASK, 0, &ion_hnd);
      #else
          #error "Compound page heap mask not defined"
      #endif

   .. code-block:: none

      #### file: Android.mk

      LOCAL_SHARED_LIBRARIES += libion



.. Add blank line before section header
|

.. _cpa_integration_juno_example:

Juno Example
------------

At present, CPA has only been integrated into a downstream version of the 3.18
Linux Kernel (Linaro Juno 3.18 kernel: `<https://git.linaro.org/landing-teams/
working/arm/kernel.git/log/?h=arm-juno-mali-fpga>`_). The generic instructions,
above, are exemplified by the following commits for Juno:

1. `Add compound page heap to ION <https://git.linaro.org/landing-teams/working/
   arm/kernel.git/commit/drivers/staging/android?h=arm-juno-mali-fpga&
   id=2b8cff766e6857b26603dfecd3b562ac1c8b60f5>`_
2. `Expose CPA to ION core <https://git.linaro.org/landing-teams/working/arm/
   kernel.git/commit/drivers/staging/android?h=arm-juno-mali-fpga&
   id=cbf003119fa4785dea78c708e1b2bde2823491e7>`_
3. `Add heap to Juno ION device <https://git.linaro.org/landing-teams/working/
   arm/kernel.git/commit/drivers/staging/android?h=arm-juno-mali-fpga&
   id=2e086788b11fe6428b289aa99363b2de1c8f57da>`_
4. `Update CPA to v1 <https://git.linaro.org/landing-teams/working/arm/
   kernel.git/commit/drivers/staging/android?h=arm-juno-mali-fpga&
   id=618d9cb1670bdf465fe73faa1b0d3d5eb7fd2843>`_
5. `Re-configure CPA for Juno ION device (to reflect changes in CPA)
   <https://git.linaro.org/landing-teams/working/arm/kernel.git/commit/drivers/
   staging/android?h=arm-juno-mali-fpga&
   id=0a5304e1d0afcc20abe00f92631f889afc5800ae>`_



  WARNING: all steps are required since CPA was updated after first revision
  (e.g. use of ``ion_platform_heap`` private data for CPA heap changed between
  3rd and 4th commit)




