"""Give the console a fallback writer that uses the driver's own transfer path.

The console writes 512-byte sectors with its own polled-PIO sequence, inheriting the card state
the firmware left.  Once NativeMsdc has run, that sequence fails: three attempts, then emmc_failed
and silence for the rest of the boot.  The driver restores CFG and INTEN after every transfer, so
the registers are not the problem -- the console's own command sequence is.

The driver's transfer() is the path the whole port already depends on, so the console falls back
to it: the patch declares a weak native_msdc_console_sink(lba, data) and NativeMsdc implements it
by issuing the same single-block write it uses for everything else.
"""
import pathlib

# 1. console side: a weak sink and a fallback in the write path
patch = pathlib.Path('/mnt/d/aios/_work/UniXNU-dandelion/patches/xnu/device-emmc-log-console.patch')
text = patch.read_text()

old = "+static bool emmc_failed;"
new = ("+static bool emmc_failed;\n"
       "+/*\n"
       "+ * Provided by the storage driver (weak, so a board without it still links).  The console's\n"
       "+ * own polled-PIO sequence stops working once the driver has touched the card, and its\n"
       "+ * recovery only buys three attempts; the driver's transfer path is the one the whole port\n"
       "+ * trusts, so a failed block is retried through it.\n"
       "+ */\n"
       "+extern int native_msdc_console_sink(uint32_t lba, const void *data) __attribute__((weak));")
assert old in text
text = text.replace(old, new, 1)

# fallback inside emmc_write_block: after the console's own attempt fails
old = """+static int
+emmc_write_block(uint32_t lba, const void *buffer)"""
assert old in text

# add the fallback right where the block write reports success/failure
old_tail = """+	if (emmc_wait_ready(1000) != 0) {
+		return -1;
+	}"""
if old_tail not in text:
    # locate the end of emmc_write_block instead: retry loop in emmc_flush already handles it,
    # so wire the fallback through emmc_flush's attempt loop
    pass

# simpler and robust: make emmc_flush try the driver sink after its own three attempts
old_flush = """+	if (attempt == 3) {
+		emmc_failed = true;
+		return;
+	}"""
new_flush = """+	if (attempt == 3) {
+		/* The console's own sequence no longer works; hand the block to the driver. */
+		if (native_msdc_console_sink &&
+		    native_msdc_console_sink(emmc_log_lba + 1 + emmc_index, emmc_sector) == 0) {
+			if ((emmc_index % EMMC_HEADER_EVERY) == 0) {
+				emmc_write_header();
+			}
+			return;
+		}
+		emmc_failed = true;
+		return;
+	}"""
assert old_flush in text
text = text.replace(old_flush, new_flush, 1)
patch.write_text(text, encoding='utf-8', newline='\n')
print('console: falls back to the driver sink after its own attempts fail')

# 2. driver side: implement the sink on top of transfer()
driver = pathlib.Path('/mnt/d/aios/_work/UniXNU-dandelion/package/NativeMsdc.cpp')
source = driver.read_text(encoding='utf-8')
if 'native_msdc_console_sink' not in source:
    anchor = """extern "C" int native_ufs_block_authenticates (IOService *provider, const char *uuid, uint8_t *digest)"""
    assert anchor in source
    addition = """// The eMMC console's own polled-PIO sequence stops working once this driver has touched the card,
// so the console patch hands a failed block here.  This is the same single-block write the whole
// port already uses, so it works wherever the driver does.
extern "C" int native_msdc_console_sink (uint32_t lba, const void *data)
{
  if (!data || !gHost.layoutValid) return -1;
  HostSession session;
  return transfer (lba, 1, const_cast<uint8_t *> (static_cast<const uint8_t *> (data)), true) ==
             kIOReturnSuccess ? 0 : -1;
}

""" + anchor
    source = source.replace(anchor, addition, 1)
    driver.write_text(source, encoding='utf-8', newline='\n')
    print('driver: native_msdc_console_sink implemented on transfer()')
