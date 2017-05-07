PuTTY README
============

This is the README file for the PuTTY MSI installer distribution. If
you're reading this, you've probably just run our installer and
installed PuTTY on your system.

What should I do next?
----------------------

If you want to use PuTTY to connect to other computers, or use PSFTP
to transfer files, you should just be able to run them from the
Start menu.

If you want to use the command-line file transfer utility PSCP, you
will need to run this from a Command Prompt or equivalent, because it
will not do anything useful without command-line options telling it
what files to copy to and from where. You can do this by just running
the command 'pscp' from a Command Prompt, if you used the installer's
option to put the PuTTY installation directory on your PATH.
Alternatively, you can always run pscp.exe by its full pathname, e.g.
"C:\Program Files\PuTTY\pscp.exe".

(Note that a Command Prompt that was already open before you ran the
installer will not have inherited the update of PATH.)

Some versions of Windows will refuse to run HTML Help files (.CHM)
if they are installed on a network drive. If you have installed
PuTTY on a network drive, you might want to check that the help file
works properly. If not, see http://support.microsoft.com/kb/896054
for information on how to solve this problem.

What do I do if it doesn't work?
--------------------------------

The PuTTY home web site is

    https://www.chiark.greenend.org.uk/~sgtatham/putty/

Here you will find our list of known bugs and pending feature
requests. If your problem is not listed in there, or in the FAQ, or
in the manuals, read the Feedback page to find out how to report
bugs to us. PLEASE read the Feedback page carefully: it is there to
save you time as well as us. Do not send us one-line bug reports
telling us `it doesn't work'.
