// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Find the functions Ghidra's first pass missed in a PSP executable's .text and re-run analysis.
// Each pass walks .text and starts a function at every non-zero word that no function
// contains and that follows a function's last byte or zero padding (the first word of each
// gap), so the gaps shrink pass by pass. Repeats until nothing new is found, then analyses.
// Usage (headless postScript, without -readOnly): FindPspFunctions.java
// @category gd-game-24

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;

public class FindPspFunctions extends GhidraScript {
	@Override
	public void run() throws Exception {
		MemoryBlock text = currentProgram.getMemory().getBlock(".text");
		Memory mem = currentProgram.getMemory();
		Listing listing = currentProgram.getListing();
		int total = 0;
		for (int pass = 0; pass < 40; pass++) {
			int made = 0;
			Address a = text.getStart();
			Address end = text.getEnd();
			while (a.compareTo(end) < 0) {
				monitor.checkCancelled();
				if (getFunctionContaining(a) == null) {
					int w = mem.getInt(a);
					Address p = a.subtract(4);
					boolean gapStart = a.equals(text.getStart()) || getFunctionContaining(p) != null ||
						mem.getInt(p) == 0;
					if (w != 0 && gapStart) {
						if (listing.getInstructionAt(a) == null) {
							listing.clearCodeUnits(a, a.add(3), false);
							new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
						}
						if (createFunction(a, null) != null) {
							made++;
						}
					}
				}
				a = a.add(4);
			}
			total += made;
			println("FindPspFunctions: pass " + pass + " created " + made);
			if (made == 0) {
				break;
			}
		}
		analyzeChanges(currentProgram);
		println("FindPspFunctions: created " + total + " functions, now " +
			currentProgram.getFunctionManager().getFunctionCount());
	}
}
