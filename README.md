Med (Memory Editor)
===================

Date: 2017-01-17

There was a memory editor in Windows, that was Game Master. But it was not a freeware. And there is a freeware memory editor, it is ArtMoney. But it is also for Windows only. In Linux, there is only one memory editor, scanmem with GameConqueror as the GUI. However, there is a lot of limitation from scanmem and GameConqueror. Thus, that is why I decided to create one which can fit my needs.

Med is still in the development, it is not stable. Anyone can help to improve the program.


Usage
=====

`med-qt` is the GUI memory editor. In order to use it, please use **sudo**.


Interface
---------
The interface can briefly separated into to panes. Left pane is the result from the scan; right pane is the memory addresses that we intended to store and save, or open from the file.


Scanning & filtering
--------------------
1. Before scanning, please click "Process" button to choose the process that you want to scan for the memory.
2. After choosing the process, you can type in the value that you want to **scan**. (For the current stage, the only data types allowed are int8, int16, int32, float32, and float64.) For example, we can scan for the gold amount.
3. After we make some changes in the game, you can **filter** it.

Manage address
--------------
The scanned or stored memory addresses, you can

1. edit the data type.
2. modify the memory address.
3. edit the value.

At the right pane, you can

1. add **new** memory address and edit manually.
2. **delete** the selected memory address.
3. **shift** all the memory address in the right pane to a target position. For example, one of the item, namely Gold, memory address is 0x20de9b94, after you restart the game, the memory address is changed to 0x20c3cb80. In order to shift the memory together with other items (in the right pane), copy paste 0x20de9b94 to the entry after the **Delete** button, and 0x20c3cb80 to the entry before **Shift** button. Then click **Shift**, all the address wil be shifted.
(If mouseover the entries, you will get the tooltips.)


Save/open file
--------------
The JSON file is used. Please save the file in the JSON extension.


Build Instruction
=================
This program requires **GCC** (C++ compiler), **Qt5**, and **JSONPP**.

1. In the directory that contains the source code including `CMakeLists.txt`,

```
mkdir build && cd build  
cmake ../  
make  
```

2. To run the GUI, make sure the `main-qt.ui` and `process.ui` are together with the compiled binary files, and enter

`sudo ./med-qt`


TODO
====
1. Memory editor dialog (view any memory as block).
2. Scan by address (from/to).
3. Scan by array.
4. Scan by string.
5. Scan by changes.
6. Scan by pointer(?)

