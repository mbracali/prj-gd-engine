// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Dumps the QScript C-function tables: runs of {u32 name pointer, u32 code
// pointer} pairs where the name is a C string and the code pointer lies in
// executable memory. Writes <out>/cfuncs.tsv (name, function address,
// table entry address).
// Usage (headless postScript): DumpCFuncs.java <output_dir> [start end]
//   default range: the whole program.
// @category gd-game-04

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

public class DumpCFuncs extends GhidraScript {

	private String cstring(Memory mem, long a) {
		try {
			Address ad = toAddr(a);
			MemoryBlock b = mem.getBlock(ad);
			if (b == null || b.isExecute()) {
				return null;
			}
			StringBuilder sb = new StringBuilder();
			for (int i = 0; i < 96; i++) {
				int c = mem.getByte(ad.add(i)) & 0xff;
				if (c == 0) {
					break;
				}
				if (c < 0x20 || c > 0x7e) {
					return null;
				}
				sb.append((char) c);
			}
			if (sb.length() < 2) {
				return null;
			}
			char f = sb.charAt(0);
			if (!Character.isLetter(f) && f != '_') {
				return null;
			}
			return sb.toString();
		} catch (Exception e) {
			return null;
		}
	}

	private boolean isCode(Memory mem, long a) {
		if ((a & 3) != 0) {
			return false;
		}
		MemoryBlock b = mem.getBlock(toAddr(a));
		return b != null && b.isExecute();
	}

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		Path out = Paths.get(args[0]);
		Files.createDirectories(out);
		Memory mem = currentProgram.getMemory();
		int count = 0;
		try (PrintWriter w = new PrintWriter(Files.newBufferedWriter(out.resolve("cfuncs.tsv"), StandardCharsets.UTF_8))) {
			w.println("name\tfunction\tentry");
			for (MemoryBlock b : mem.getBlocks()) {
				if (b.isExecute() || !b.isInitialized()) {
					continue;
				}
				long s = b.getStart().getOffset(), e = b.getEnd().getOffset();
				if (args.length >= 3) {
					s = Math.max(s, Long.parseLong(args[1].replaceFirst("^0x", ""), 16));
					e = Math.min(e, Long.parseLong(args[2].replaceFirst("^0x", ""), 16));
				}
				for (long a = (s + 3) & ~3L; a + 8 <= e; a += 4) {
					long np = mem.getInt(toAddr(a)) & 0xffffffffL;
					long fp = mem.getInt(toAddr(a + 4)) & 0xffffffffL;
					if (!isCode(mem, fp)) {
						continue;
					}
					String n = cstring(mem, np);
					if (n == null) {
						continue;
					}
					w.printf("%s\t%08x\t%08x%n", n, fp, a);
					count++;
					a += 4;
				}
			}
		}
		println("cfuncs: " + count);
	}
}
