# XorPlayer
A simple program that reads XorDev shaders in the audio-reactive .milk format :)<br>
Download the latest version [here](https://github.com/milkdrop2077/XorPlayer/releases/download/XorPlayer/XorPlayer.zip).<br><br>
<img width="328" height="346" src="https://github.com/milkdrop2077/XorPlayer/raw/main/XorPlayer.jpg"><br>
XorPlayer requires the DirectX 9 runtime DLLs to run.<br>
Install them by running DirectX\DXSETUP.exe (included in the zip) or directly from [microsoft.com](https://www.microsoft.com/en-us/download/details.aspx?id=8109).<br><br>
Press F8 to activate SPOUT.<br>
Press F1 for help.<br>
# How to compile the source code<br>
- Install Visual Studio Community 2022<br>
  Download from: https://visualstudio.microsoft.com/vs/community/<br>
  During setup, ensure these options are selected:<br>
    - C++ MFC build tools<br>
    - Game Development with C++<br>
- Install the DirectX SDK (June 2010)<br>
  Download from: https://www.microsoft.com/en-ca/download/details.aspx?id=6812<br>
- Build the Project<br>
  Open XorPlayer.sln in Visual Studio.<br>
- Compile the solution.<br>
  If successful, the executable will be generated at:<br>
  XorPlayer\vis_milk2\Release\XorPlayer.exe<br>
  The compiled XorPlayer.exe requires the XorFiles folder<br>
  (included in XorPlayer\vis_milk2\Release\XorFiles).<br>
# 
XorPlayer is based on [MilkDrop 3.0](https://github.com/milkdrop2077/MilkDrop3) and [Spout](https://github.com/leadedge/Spout2). 

