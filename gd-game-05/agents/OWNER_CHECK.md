# Owner check: the Rock Band guitar inside the launcher (GD05-061)

Needs the next launcher package (the coordinator's build, with gd05's
"Kernel device" report line). Guitar plugged in, nothing else changed.

1. **Capture through the launcher.** In a terminal, from the launcher's folder:

       ./"Guitar Dream Universal Launcher" --play gd05 --exit-after \
           --iso ~/Development/prj-gd-engine/original_isos/"X360 - Rock Band (USA).iso" -- --inputcapture

   On the capture screen: strum down 5 times, strum up 5 times, press green,
   red, yellow, blue, orange once each, move the whammy, press Start. Then Esc.

2. **A song through the launcher.** Start the launcher as usual, Rock Band,
   Quickplay any song. In the song strum a few notes on the guitar, then press
   the guitar's **Start**: the pause menu (RESUME / RESTART / QUIT) must show;
   green on RESUME goes back to the song. Then Esc (pause again) and quit.

3. **Send the two files** from `~/.local/share/gd-engine/gd05/`:
   `input-capture.txt` (step 1) and `controllers-report.txt` (step 2).

## What the files should say

Working (what the tests here show through your launcher build):

    Controller 4: Harmonix Rock Band Guitar
      Kernel device /dev/input/event9: 34 events from the controller, SDL joystick events 34
    Events seen this session (joystick / gamepad), by controller:  4: 34 / 34

and in input-capture.txt lines like

    12.345678 s  gamepad 4 dpdown down
    12.345690 s    -> Strum down, player 1, strummed (guitar), in a song
    12.345701 s  raw controller 4 hat 0 = 4 (down) (t 12345 ms)

(the numbers vary; what matters is that they are not 0 and the two counts agree).

If it is still broken, the Kernel device line says where:

- `0 events from the controller ... -> The controller sent nothing to the
  system while the game ran`: the guitar's events never reached Linux while
  the game ran (cable, USB port, the guitar, the xpad driver). Then please
  also say whether the same guitar works in Guitar Hero Metallica (gd10) in
  the same launcher session.
- `N events from the controller, SDL joystick events 0 -> The system had the
  controller's events but SDL gave the game none`: the loss is in SDL /
  the game; the file has what is needed to fix it.
