// -----------------------------------------------------------------------------------------
// Sapera++ console grab example
// 
//    This program shows how to grab images from a camera into a buffer in the host
//    computer's memory, using Sapera++ Acquisition and Buffer objects, and a Transfer 
//    object to link them.  Also, a View object is used to display the buffer.
//
// -----------------------------------------------------------------------------------------

// Disable deprecated function warnings with Visual Studio 2005
#if defined(_MSC_VER) && _MSC_VER >= 1400
#pragma warning(disable: 4995)
#endif


#include <iostream>
#include <fstream>
#include "stdio.h"
#include "conio.h"
#include "sapclassbasic.h"
#include "ExampleUtils.h"
#include <WinSock.h>
using namespace std;

// Restore deprecated function warnings with Visual Studio 2005
#if defined(_MSC_VER) && _MSC_VER >= 1400
#pragma warning(default: 4995)
#endif


#define PORT 8935
#define BUFSIZE 2048
#define NSLOTS 100  //maximum near 8000
#define DEFAULT_FILENAME "Grabconsole.tiff"
#pragma comment(lib, "Ws2_32.lib")


// Static Functions
static void AcqCallback(SapXferCallbackInfo *pInfo);
static void StartFrameCallback(SapAcqCallbackInfo *pInfo);
static BOOL GetOptions(int argc, char *argv[], char *acqServerName, UINT32 *pAcqDeviceIndex, char *configFileName);
static BOOL GetOptionsFromCommandLine(int argc, char *argv[], char *acqServerName, UINT32 *pAcqDeviceIndex, char *configFileName);
static float buffToFloat(char* buf);
static BOOL newName(char* fileName);
static void setBaseName(char*baseName, char*outFileName, char*syncFileName);


int start,endCount,duration,fps  = 0;
BOOL firstFrame = TRUE;

/*
*  Callback function - StartOfFrame event
*  The function will record the timestamp(in ms) of the system when 
*  each callback is received and then calculate an approximate FPS rate
*  from two consecutive StartOfframe events.
*  NOTE : The FPS is only an approximation due to delays in callbacks
*  and it should not be used as the actual FPS achieved by the system.
*/
static void StartFrameCallback(SapAcqCallbackInfo *pInfo)
{
   //for the first start of frame record the start time.
   if (firstFrame)
   {
      firstFrame = FALSE;
      start = GetTickCount();
      return;
   }
   endCount = GetTickCount();
   duration = endCount - start;
   start = endCount; 

   //update FPS only if the value changed. 1000 is used because the duration is in ms.
   if (fps != 1000/duration)
   {
      fps = 1000/duration;
      printf("Approximate FPS = %d \r",fps);
   }

}


int main(int argc, char* argv[])
{
   SapAcquisition		*Acq=NULL;
   SapAcqDevice			*AcqDevice=NULL;
   SapBuffer			*Buffers=NULL;
   SapTransfer			*Xfer=NULL;
   SapView				*View=NULL;



   struct sockaddr_in myaddr;      // our address 
   struct sockaddr_in remaddr;     // remote address 
   int addrlen = sizeof(remaddr);            // length of addresses 
   int recvlen;                    // # bytes received 

   SOCKET fd;                         // our socket 
   char buf[BUFSIZE];     // receive buffer 
   bool listenUDP = true;  //
   bool nameSet = false;
   int frame = 0;
   bool bView = false;
   int counter = 0; //Used when taking more images than buffer size
   ofstream file;
   

   std::vector<std::string> syncBuf;
   WSADATA wsda;
   WSAStartup(MAKEWORD(1, 1), &wsda);
   // create a UDP socket 
   fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   if (fd ==SOCKET_ERROR) {
	   perror("cannot create socket\n");
	   printf("error socket");
	   Sleep(500);
	   return 0;
   }

   // bind the socket to any valid IP address and a specific port 

   memset((char *)&myaddr, 0, sizeof(myaddr));
   myaddr.sin_family = AF_INET;
   myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
   myaddr.sin_port = htons(PORT);

   if (bind(fd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
	   perror("bind failed");
	   return 0;
   }


   //char     acqServerName[CORSERVER_MAX_STRLEN], configFilename[MAX_PATH];
   UINT32   acqDeviceNumber;
   char*    acqServerName=new char[CORSERVER_MAX_STRLEN];
	char*    configFilename=new char[MAX_PATH];
	char* baseName = new char[MAX_PATH];
	char* outFileName = new char[MAX_PATH];
	char* syncFileName = new char[MAX_PATH];

	CorStrncpy(baseName, "SaperaCamera", MAX_PATH);

   printf("Sapera Console Grab Example (C++ version)\n");
   
   // Call GetOptions to determine which acquisition device to use and which config
   // file (CCF) should be loaded to configure it.
   // Note: if this were an MFC-enabled application, we could have replaced the lengthy GetOptions 
   // function with the CAcqConfigDlg dialog of the Sapera++ GUI Classes (see GrabMFC example)
   if (!GetOptions(argc, argv, acqServerName, &acqDeviceNumber, configFilename))
   {
      printf("\nPress any key to terminate\n");
      CorGetch(); 
      return 0;
   }

  
   SapLocation loc(acqServerName, acqDeviceNumber);

   if (SapManager::GetResourceCount(acqServerName, SapManager::ResourceAcq) > 0)
   {
		Acq			= new SapAcquisition(loc, configFilename);
		Buffers		= new SapBuffer(NSLOTS, Acq);
		View		= new SapView(Buffers, SapHwndAutomatic);
		Xfer		= new SapAcqToBuf(Acq, Buffers, AcqCallback, View);

      // Create acquisition object
      if (Acq && !*Acq && !Acq->Create())
         goto FreeHandles;

   }

   //register an acquisition callback
   if (Acq)
      Acq->RegisterCallback(SapAcquisition::EventStartOfFrame,StartFrameCallback,NULL);

   if (SapManager::GetResourceCount(acqServerName, SapManager::ResourceAcqDevice) > 0)
   {
	   if (strcmp(configFilename,"NoFile") == 0)
		   AcqDevice	= new SapAcqDevice(loc, FALSE);
	   else
		   AcqDevice	= new SapAcqDevice(loc, configFilename);

	   Buffers		= new SapBufferWithTrash(NSLOTS, AcqDevice);
	   if (bView){
		   View = new SapView(Buffers, SapHwndAutomatic);
	   }
	   Xfer		   = new SapAcqDeviceToBuf(AcqDevice, Buffers, AcqCallback, View);

      // Create acquisition object
      if (AcqDevice && !*AcqDevice && !AcqDevice->Create())
         goto FreeHandles;

   }

   // Create buffer object
   if (Buffers && !*Buffers && !Buffers->Create())
      goto FreeHandles;

   // Create transfer object
   if (Xfer && !*Xfer && !Xfer->Create())
      goto FreeHandles;

   // Create view object
   if (View && !*View && !View->Create())
      goto FreeHandles;


    //listen to socket;
   
  
   char fNum[5];
   SYSTEMTIME time;
   float timestart, timestamp;
   while (listenUDP){
	   
	   recvlen = recvfrom(fd, buf, BUFSIZE, 0, (struct sockaddr *)&remaddr, &addrlen);
	   /*Medthod 2: Snap and Save at Each UDP. see other method in GrapCPPv4*/
	   if (recvlen > 4){
		   if (nameSet == true){
			   //if already set, close current file and start new one.
			   file.close();
			   counter = 0;
			   frame = 0;
			   Xfer->Init();
			   Xfer->Wait(70);
		   }
		   CorStrncpy(baseName, buf, recvlen + 1);
		   setBaseName(baseName, outFileName, syncFileName);
		   nameSet = true;
		   file.open(syncFileName);
		   printf("File Name set to %s \n", baseName);
		   GetSystemTime(&time);
		   timestart = (float)time.wMilliseconds / 1000;
		   timestart += time.wSecond ;
	   }else if ((recvlen == 4)&&(nameSet==true)) {
		   float fSync = buffToFloat(buf);
		   
		   if (fSync > 0){
			   //if receiving a pos (+) value, snap and save
			   //else close file
			   syncBuf.push_back(std::to_string(fSync));
			   GetSystemTime(&time);
			   Xfer->Snap();
			   Xfer->Wait(70);
			   CorStrncpy(outFileName, baseName, MAX_PATH);
			   CorStrncat(outFileName, "_", MAX_PATH);
			   sprintf_s(fNum, sizeof(fNum),"%05d", (frame + counter));
			   CorStrncat(outFileName, fNum, MAX_PATH);
			   CorStrncat(outFileName, ".tiff", MAX_PATH);
			   Buffers->Save(outFileName, "-format tiff", frame, 1);
			   timestamp = (float)time.wMilliseconds / 1000;
			   timestamp += time.wSecond;
			   file << fNum << "\t" << syncBuf[frame].c_str() << "\t" << std::to_string(timestamp - timestart) << "\n";
			   frame++;
			   printf("%s %d %s\n ", "Program Running: ", counter + frame, " Captured");
		   }else{
			   nameSet = false;
			   file.close();
			   counter = 0;
			   frame = 0;
			   Xfer->Init();
			   Xfer->Wait(70);
		   }
		   if (frame == NSLOTS){
			   counter += frame;
			   frame = 0;
			   Xfer->Init();
			   Xfer->Wait(70);
			   syncBuf.clear();
		   }
	   }
	   else{
		   printf("No File Name Selected \n");
	   }
	   
	   /*End of Method 2*/
   }
  
   closesocket(fd);

   WSACleanup();
	

FreeHandles:   
   //printf("Program Ended\n");
   //printf("Press any key to terminate\n");
   //CorGetch();
   
   if (file)
	   file.close();
	//unregister the acquisition callback
   if (Acq)
	   Acq->UnregisterCallback();

	// Destroy view object
	if (View && *View && !View->Destroy()) return FALSE;

	// Destroy transfer object
	if (Xfer && *Xfer && !Xfer->Destroy()) return FALSE;

	// Destroy buffer object
	if (Buffers && *Buffers && !Buffers->Destroy()) return FALSE;

	// Destroy acquisition object
	if (Acq && *Acq && !Acq->Destroy()) return FALSE;

	// Destroy acquisition object
	if (AcqDevice && *AcqDevice && !AcqDevice->Destroy()) return FALSE;

	// Delete all objects
	if (View)		delete View; 
	if (Xfer)		delete Xfer; 
	if (Buffers)	delete Buffers; 
	if (Acq)		delete Acq; 
	if (AcqDevice)	delete AcqDevice; 

   return 0;
}

static void AcqCallback(SapXferCallbackInfo *pInfo)
{

	SapView *pView= (SapView *) pInfo->GetContext();

	// Resfresh view
	pView->Show();
}

static BOOL GetOptions(int argc, char *argv[], char *acqServerName, UINT32 *pAcqDeviceIndex, char *configFileName)
{
   // Check if arguments were passed
   if (argc > 1)
      return GetOptionsFromCommandLine(argc, argv, acqServerName, pAcqDeviceIndex, configFileName);
   else
      return GetOptionsFromQuestions(acqServerName, pAcqDeviceIndex, configFileName);
}

static BOOL GetOptionsFromCommandLine(int argc, char *argv[], char *acqServerName, UINT32 *pAcqDeviceIndex, char *configFileName)
{
   // Check the command line for user commands
   if ((strcmp(argv[1], "/?") == 0) || (strcmp(argv[1], "-?") == 0))
   {
      // print help
      printf("Usage:\n");
      printf("GrabCPP [<acquisition server name> <acquisition device index> <config filename>]\n");
      return FALSE;
   }

   // Check if enough arguments were passed
   if (argc < 4)
   {
      printf("Invalid command line!\n");
      return FALSE;
   }


   // Validate server name
   if (SapManager::GetServerIndex(argv[1]) < 0)
   {
      printf("Invalid acquisition server name!\n");
      return FALSE;
   }

   // Does the server support acquisition?
   int deviceCount = SapManager::GetResourceCount(argv[1], SapManager::ResourceAcq);
   int cameraCount = SapManager::GetResourceCount(argv[1], SapManager::ResourceAcqDevice);

   if (deviceCount+cameraCount == 0)
   {
      printf("This server does not support acquisition!\n");
      return FALSE;
   }

   // Validate device index
   if (atoi(argv[2]) < 0 || atoi(argv[2]) >= deviceCount+cameraCount)
   {
      printf("Invalid acquisition device index!\n");
      return FALSE;
   }

	if (cameraCount==0)
	{
	 // Verify that the specified config file exist
  	 OFSTRUCT of = {0};
	 if (OpenFile(argv[3], &of, OF_EXIST) == HFILE_ERROR)
	 {
		  printf("The specified config file (%s) is invalid!\n", argv[3]);
		  return FALSE;
	 }
	}

   // Fill-in output variables
   CorStrncpy(acqServerName, argv[1], CORSERVER_MAX_STRLEN);
   *pAcqDeviceIndex = atoi(argv[2]);
	if (cameraCount==0)
   CorStrncpy(configFileName, argv[3], MAX_PATH);

	char configPath[MAX_PATH];
	GetEnvironmentVariable("SAPERADIR", configPath, sizeof(configPath));
	CorStrncat(configPath, "\\CamFiles\\User\\", sizeof(configPath));
	CorStrncpy(configFileName, configPath, MAX_PATH);
	CorStrncat(configFileName, argv[3], MAX_PATH);

   return TRUE;
}

float buffToFloat(char * buffer)
{
	float a;
	*((unsigned char *)&a + 3) = buffer[0];
	*((unsigned char *)&a + 2) = buffer[1];
	*((unsigned char *)&a + 1) = buffer[2];
	*((unsigned char *)&a + 0) = buffer[3];
	return a;
}

BOOL newName(char*fileName)
{
	char* testFileName = new char[MAX_PATH];
	CorStrncpy(testFileName, fileName, MAX_PATH);
	CorStrncat(testFileName, ".sync", MAX_PATH);
	if (FILE *file = fopen(testFileName, "r")) {
		fclose(file);
		return false;
	}
	else {
		return true;
	}
}

void setBaseName(char* baseName, char* outFileName, char*syncFileName){
	//Test if fileName is taken
	//If it is generate new one by appending additional number (up to 10)
	CorStrncpy(outFileName, baseName, MAX_PATH);
	if (!newName(outFileName)){
		int counter = 1;
		CorStrncat(outFileName, "_", MAX_PATH);
		CorStrncat(outFileName, to_string(counter).c_str(), MAX_PATH);
		while (counter<10 && !newName(outFileName)){
			counter++;
			CorStrncpy(outFileName, baseName, MAX_PATH);
			CorStrncat(outFileName, "_", MAX_PATH);
			CorStrncat(outFileName, to_string(counter).c_str(), MAX_PATH);
		}
	}

	//Test again if that encounter actually gave a new name
	//Otherwise use default name
	if (!newName(outFileName)){
		CorStrncpy(outFileName, "SaperaCamera", MAX_PATH);
	}
	CorStrncpy(baseName, outFileName, MAX_PATH);
	CorStrncpy(syncFileName, outFileName, MAX_PATH);
	CorStrncat(syncFileName, ".sync", MAX_PATH);
}
