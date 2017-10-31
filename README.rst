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


   The latest version of CPA (and its associated tests) should be integrated
   into a 3.18 Linux Kernel which contains ION. Porting might be required
   for other Linux Kernel versions.


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






.. Add blank line before section header
|

CPA logging system
==================

CPA is also able to show its current working state through the ION heap
``debug_show`` interface. This can be accessed by mounting debugfs
(*/sys/kernel/debug*, for example) and reading
*/sys/kernel/debug/ion/heaps/ion_compound_page*. This file contains
performance data and module working state.

The CPA logging system can be enabled via configuring
``CONFIG_ION_COMPOUND_PAGE_STATS`` in kernel.

Performance data and state, from sys file ``ion_compound_page``, will be
shown in the following format:

.. code-block:: none

    root@juno:/ # cat /sys/kernel/debug/ion/heaps/compound_page
              client              pid             size
    ----------------------------------------------------
    ----------------------------------------------------
    orphaned allocations (info is from last known client):
    ----------------------------------------------------
      total orphaned                0
              total                 0
    ----------------------------------------------------
    Free pool:
      0 times depleted
      0 page(s) in pool - 0 B (0)
      0 partial(s) in use
      Unused in partials - 0 B (0)
      Partial bitmaps:
    Shrink info:
      Shrunk performed 0 time(s)
      0 page(s) shrunk in total
    Usage stats:
      Max time spent to perform an allocation: 42452220 ns
      Max time spent to allocate a single page from kernel: 32746920 ns
      Soft alloc failures: 0
      Hard alloc failures: 194
      Allocations:
        Total number of allocs seen: 1429
        Live allocations: 0
        Accumulated bytes requested: 2.84 GiB (3058683904)
        Accumulated bytes committed: 2.84 GiB (3058683904)
        Live bytes requested: 0 B (0)
        Live bytes committed: 0 B (0)
      Distribution:
      0 page(s):
        Total number of allocs seen: 3
        Live allocations: 0
        Accumulated bytes requested: 2.98 MiB (3133440)
        Accumulated bytes committed: 2.98 MiB (3133440)
        Live bytes requested: 0 B (0)
        Live bytes committed: 0 B (0)
      1 page(s):
        Total number of allocs seen: 1425
        Live allocations: 0
        Accumulated bytes requested: 2.78 GiB (2988441600)
        Accumulated bytes committed: 2.78 GiB (2988441600)
        Live bytes requested: 0 B (0)
        Live bytes committed: 0 B (0)
      15 page(s):
        Total number of allocs seen: 1
        Live allocations: 0
        Accumulated bytes requested: 64.0 MiB (67108864)
        Accumulated bytes committed: 64.0 MiB (67108864)
        Live bytes requested: 0 B (0)
        Live bytes committed: 0 B (0)

..

  NOTE: The compound page stats from above were captured immediately after
  running the CPA tests (as described below) on Juno platform.



.. Add blank line before section header
|

CPA Tests (3.18 Linux Kernel)
=============================

There are two primary tests provided for CPA:

* Basic test
* Fragmentation problem test


**Basic test:**

This is a standalone user-space test to allocate compound pages through ION
with mask ``ION_HEAP_TYPE_COMPOUND_PAGE_MASK``. CPA allocations vary in size
and it's important to confirm that each allocation is fulfilled by the
ION CPA heap and that each 2MB granule is physically contiguous. All allocations
are passed to the kernel module to verify that the large page size is 2MB.
Furthermore, it's necessary to ensure that CPA fails to allocate memory
gracefully when system memory is exhausted. It's possible to check for errors
(such as process hang, kernel panic, etc.) in this situation. Android low memory
killer should be disabled during this test to ensure that the system doesn't
try and free memory during the test.

  NOTE: Page order in ``ion_cpa_platform_data`` must be set to ``9`` for this
  test (which expects 2MB pages, 4kB * 2^9) or the kernel module updated to
  match expected page order.


**Fragment problem test:**

This test validates that CPA behaves correctly when the system memory is heavily
fragmented. CPA might fail to allocate large pages even if there is enough
system memory free.


*Test steps:*

1. Test kernel module implements a function to force memory system get into
   fragment situation by allocating and freeing pages.

2. User space then attempts 200 times to allocate 2MB pages through CPA.
   The test then records how many allocations failed. This shall be referred to
   as ``t1``.

3. The test then attempts to free 200 page units we hold in test kernel
   module.

4. Step 2 is then repeated as the tests attempts to allocate 2MB pages 200
   times, then records how many allocations failed, this shall be referred to as
   ``t2``.

5. The test compares the failed times ``t1&t2``. If ``t2`` is less than ``t1``,
   CPA was affected by fragmentation problem in step 2.



.. Add blank line before section header
|

Building Tests
--------------

CPA test code contains two parts:

* User-space native application
* Kernel-space module

Compile Android native user-space application as follows:

.. code-block:: bash

    # Setup Android build environment
    cd <path-to-android-tree>
    source build/envsetup.sh
    lunch <lunch-combo>

    # Link to CPA Tests user code
    mkdir -p <path-to-android-tree>/vendor/<vendor>/
    ln -sf <path-to-cpa-tests>/test/user <path-to-android-tree>/vendor/<vendor>/cpa-test-user

    # Build user-space application
    cd <path-to-android-tree>/vendor/<vendor>/cpa-test-user
    mm

``test_cpa_user`` will be generated in
*<path-to-android-tree>/out/target/product/<device-name>/system/bin/*

Compile Kernel space module with:

.. code-block:: bash

    export CROSS_COMPILE=<path-to-compiler>
    export ARCH=<arch>
    export KDIR=<path-to-kernel>
    cd <path-to-cpa-tests>/test/kernel
    make

where, for example:

- ``<arch>``: ``arm`` or ``arm64``
- ``<path-to-compiler>``: ``<path-to-aarch64-gcc>/bin/aarch64-linux-gnu-`` for
  64-bit Arm


``test_cpa_kernel.ko`` will be generated in current directory.



.. Add blank line before section header
|

Running Tests
-------------
In order to achieve consistent results the following should be taken into
account:

    NOTE: Low Memory Killer should be disabled for all the tests. This can be
    done through disabling kernel configuration option
    ``CONFIG_ANDROID_LOW_MEMORY_KILLER``.

..

    NOTE: In order to achieve consistent results the CPA page pool mechanism
    should be disabled by setting lowmark, highmark and fillmark in struct
    ``ion_cpa_platform_data`` to 0. The configuration should be like as
    following:

    .. code-block:: c

       struct ion_cpa_platform_data cpa_config = {
                   .lowmark = 0,
                   .highmark = 0,
                   .fillmark = 0,
                   .align_order = 0,
                   .order = 9
       };


..

    NOTE: Before running ``test_cpa_user``, it's recommended to stop all Android
    user-space services by executing ``stop`` command from the Android shell.
    This eliminates any dynamic effect of the Android system.

1. Copy user-space application and kernel module to Android system:

   .. code-block:: none

      adb push <path-to-android-tree>/out/target/product/<device-name>/system/bin/test_cpa_user /system/bin/
      adb push <path-to-cpa-tests>/test/kernel/test_cpa_kernel.ko <path-to-module>/

2. Insert kernel module:

   .. code-block:: none

      insmod <path-to-module>/test_cpa_kernel.ko

3. Run application:

   .. code-block:: none

      test_cpa_user



Example console output:

.. code-block:: none

    root@juno:/ # test_cpa_user
    CPA test start!!!

    ===================Test 1 START===================.
    1. Verify CPA, Alloc 1KB from CPA success.Verify CPA memory success.
    2. Verify CPA, Alloc 1024KB from CPA success.Verify CPA memory success.
    3. Verify CPA, Alloc 2048KB from CPA success.Verify CPA memory success.
    4. Verify CPA, Alloc 2031KB from CPA success.Verify CPA memory success.
    5. Verify CPA, Alloc 65536KB from CPA success.Verify CPA memory success.
    6. Try to allocate from CPA until system mem exhausts.
        >>> Successfully exhausted system memory and no error happened.

    ===================Test 1 END===================.



    ===================Test 2 START===================.
    Start to simulate memory fragment in test-cpa module.
        >>> After simulate memory fragment, system free memory: 5979 MB, test allocated memory: 589 MB.
        >>> System is in 2MB memory fragment situation.

        >>> Try to allocate 2MB physical contiguous memory from CPA for 200 times.
            >>> Failed 194 times. 12MB memory allocated.

        >>> Try to free 3200 KB pages in test-cpa module.
      >>> After free some tests allocated page, system free memory: 6027 MB, test allocated memory: 577 MB.

        >>> Try to allocate 2MB physical contiguous memory from CPA for 200 times.
            >>> Failed 0 times.400 MB memory allocated.

        >>> Expected result: CPA is affected by memory fragment problem.
    Stop to simulate memory fragment in test-cpa module.

    ===================Test 2 END===================.
    CPA test end!!!




.. Add blank line before section
|


.. Add blank line before section header
|

License
=======

This project is licensed under GPL-2.0.


.. Add blank line before section header
|

Contributions / Pull Requests
=============================

Contributions are accepted under GPL-2.0. Only submit contributions where you
have authored all of the code. Ensure that your employer, where applicable,
consents.
