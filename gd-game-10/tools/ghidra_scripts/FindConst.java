// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Finds where the code builds 32-bit constants (QB key checksums) with
// lis rX, hi + addi / ori rY, rX, lo, and lists each site and its function.
// Usage (headless postScript): FindConst.java 0x1aa62baa 0x5ae953dc ...
// @category gd-game-10

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

public class FindConst extends GhidraScript {
	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		long[] want = new long[args.length];
		for (int i = 0; i < args.length; i++) want[i] = Long.decode(args[i]) & 0xffffffffL;
		// Every 4-byte aligned word of initialized memory (code Ghidra did
		// not disassemble included).
		for (ghidra.program.model.mem.MemoryBlock blk : currentProgram.getMemory().getBlocks()) {
			if (!blk.isInitialized() || !blk.isExecute()) continue;
			long n = blk.getSize() / 4;
			byte[] buf = new byte[(int) blk.getSize()];
			blk.getBytes(blk.getStart(), buf);
			for (int i = 0; i + 3 < buf.length; i += 4) {
				int word = ((buf[i] & 0xff) << 24) | ((buf[i + 1] & 0xff) << 16) | ((buf[i + 2] & 0xff) << 8) | (buf[i + 3] & 0xff);
				int op = (word >>> 26) & 0x3f;
				if (op != 15) continue;
				int rd = (word >>> 21) & 31, ra = (word >>> 16) & 31;
				if (ra != 0) continue;
				long hi = word & 0xffff;
				for (int k = 1; k <= 12 && i + 4 * k + 3 < buf.length; k++) {
					int j = i + 4 * k;
					int w2 = ((buf[j] & 0xff) << 24) | ((buf[j + 1] & 0xff) << 16) | ((buf[j + 2] & 0xff) << 8) | (buf[j + 3] & 0xff);
					int op2 = (w2 >>> 26) & 0x3f;
					int src = op2 == 24 ? (w2 >>> 21) & 31 : (w2 >>> 16) & 31;
					if ((op2 != 14 && op2 != 24) || src != rd) continue;
					long lo = w2 & 0xffff;
					long v = op2 == 14 ? ((hi << 16) + (short) lo) & 0xffffffffL : ((hi << 16) | lo);
					Address at = blk.getStart().add(j);
					for (long w : want)
						if (v == w) {
							Function f = getFunctionContaining(at);
							println(String.format("0x%08x at %s in %s", w, at, f == null ? "?" : f.getName() + "@" + f.getEntryPoint()));
						}
					break;
				}
			}
		}
	}
}
