################################################################################
1. How to Build
        - get Toolchain
                get the proper toolchain packages from AOSP or Samsung Open Source or ETC.
                
                (1) AOSP Kernel
                https://source.android.com/docs/setup/build/building-kernels
                $ repo init -u https://android.googlesource.com/kernel/manifest -b common-android14-6.1
                $ repo sync
                
                (2) Samsung Open Source
                https://opensource.samsung.com/uploadSearch?searchValue=toolchain
                
                copy the following list to the root directory
                - build/
                - external/
                - prebuilts/
                - tools/

        - to Build
                $ tools/bazel run --nocheck_bzl_visibility --config=stamp --sandbox_debug --verbose_failures --debug_make_verbosity=I //projects/s5e8845:s5e8845_user_dist

2. Output files
        - out/s5e8845_user/dist
################################################################################
