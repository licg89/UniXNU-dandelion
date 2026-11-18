# ShengHidInput (vendored)

Taken from https://github.com/UniXNU/ShengHidInput at the revision the UniXNU catalog pins
(`71a40e76daeae1bb4127546f96ffa7339c7a06ff`), licensed BSD-2-Clause-Patent; the upstream LICENSE travels
with it.

It is vendored because the official builder expects the component at `drivers/ShengHidInput` in the
UniXNU tree, and this device repository is the thing that is checked out on the build host.

Do not edit `NativeHidInput.cpp`: `scripts/build-sheng-hid-input-local.sh` builds it with the official
`scripts/build-sheng-hid-input.py`, which generates the `OriginalHidDeviceABI.h` header itself and binds
the owned methods to the original IOHIDDevice slots.
