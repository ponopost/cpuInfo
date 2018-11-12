// cpuInfo.c
// #include <Ver30/Pilot.h>
#include <Pilot.h>
// #include "fp_api.h"		// TRG FlashPro
#include "palm_battery.h"
#include "palm_color.h"
#include "callback.h"
#include "res.h"

#define Creator			'cpuP'
#define PrefsId			0
#define PrefsVersion	0x003
#define PLLFREQ			0xfffff202		// PLL Frequency Control Register
#define DRAMCTRL		0xfffffc02		// DRAM Control Register

typedef struct {
	SWord		swResetDram;
	Boolean		fDramNoWait;
} PrefsType;

static Boolean CalledFromAppMode;
static Word panelCount;
static Handle panelIDsH;
static SysDBListItemType* panelIDsP = 0;
static PrefsType prefs;

//
static VoidPtr GetObjectPtr( Int objectID )
{
	FormPtr frm = FrmGetActiveForm();
	return( FrmGetObjectPtr( frm, FrmGetObjectIndex( frm, objectID )));
}

//
static void MySetStrToField( int theFieldID, char* theDrawStr )
{
	Handle   myTextH;
	CharPtr  myTextP;
	FieldPtr myFldPtr;

	ULong myStrSize = StrLen( theDrawStr ) + 1;
	myTextH = (Handle)MemHandleNew( (ULong)myStrSize );
	if( myTextH ){
		myTextP = (CharPtr)MemHandleLock( (VoidHand)myTextH );
		MemMove( myTextP, theDrawStr, myStrSize );
		myFldPtr = (FieldPtr)GetObjectPtr( theFieldID );
		FldFreeMemory( myFldPtr );
		FldSetTextHandle( myFldPtr, myTextH );
		FldEraseField( myFldPtr );
		FldDrawField( myFldPtr );
		MemHandleUnlock( (VoidHand)myTextH );
	}
}

//
static Word GetFreq()
{

	volatile Word* p;
	p = (volatile Word *)PLLFREQ;
	return (0x7fff & *p);
}

//
static Word FreqToClock( Word f )
{
	Word p, q;

	/* these are default freq values in Palm */
	if( ! f || f == 0x0123 )
		return 16;	// 16MHz
	else if( f == 0x012b )
		return 20;	// 20MHz
	else if( f == 0x0347 )
		return 33;	// 33MHz

	p = f & 0x00ff;
	q = 1;
	f = 32 * ( 14 * ( p + 1 ) + q + 1 );

	return( f / 1000 );
}

//
static Byte GetProcessor( void )
{
	// If we're not using a new include file, define things appropriately:
	#ifndef sysFtrNumProcessorID
		#define sysFtrNumProcessorID		2
		// Product id
		// 0xMMMMRRRR, where MMMM is the processor model and RRRR is the revision.
		#define sysFtrNumProcessorMask		0xFFFF0000		// Mask to obtain processor model
		#define sysFtrNumProcessor328		0x00010000		// Motorola 68328 (Dragonball)
		#define sysFtrNumProcessorEZ		0x00020000		// Motorola 68EZ328 (Dragonball EZ)
		#define	sysFtrNumProcessorVZ		0x00030000		// Motorola 68VZ328	(Dragonball VZ) - PalmOS4.0 SDK
	#endif

	DWord id, chip;
	Err err;

	err = FtrGet( sysFtrCreator, sysFtrNumProcessorID, &id );
	if( ! err ){ 
		chip = id & sysFtrNumProcessorMask;
		if( chip >= sysFtrNumProcessorVZ ){
			return( 'V' );
		} else if( chip >= sysFtrNumProcessorEZ ){
			return( 'E' );
		} else if( chip >= sysFtrNumProcessor328 ){
			return( 'N' );
		}
		return( '0' );
	}

	return( '?' );
}

//
static Boolean GetDramNoWait()
{
	Word* regDRAMC;
	regDRAMC = (Word *)DRAMCTRL;

	return (*regDRAMC & 0060) ? false : true;
}

//
static void SetDramNoWait( Boolean on )
{
	Word* regDRAMC;

	regDRAMC = (Word *)DRAMCTRL;
	if( on )
		*regDRAMC &= 0xff9f;		// zero wait-state
	else
		*regDRAMC |= 0x0060;		// three wait-states
}

//
static void SavePrefs( PrefsType* p )
{
	PrefSetAppPreferences( Creator, PrefsId, PrefsVersion, p, sizeof(PrefsType), true );
}

//
static void LoadPrefs( PrefsType* p )
{
	Word size;
	Word version;
	Boolean fDramNoWait;

	switch( GetProcessor() ){
	case 'V':
	case 'E':
		fDramNoWait = GetDramNoWait();
		break;
	default:
		fDramNoWait = false;
		break;
	}

	size = sizeof(PrefsType);
	version = PrefGetAppPreferences( Creator, PrefsId,  p, &size, true );
	if( (version != PrefsVersion) || (size != sizeof(PrefsType)) ){
		p->swResetDram = 0;
		p->fDramNoWait = fDramNoWait;
	} else {
		if( p->swResetDram != 0 ){
			p->fDramNoWait = true;
			SetDramNoWait( p->fDramNoWait );
		}
	}
}

//
static void PanelFreeData( void )
{
	if( panelIDsP ){
		MemPtrFree( panelIDsP );
		panelIDsP = NULL;
	}
}

//
static Word StartApplication( void )
{
	return 0;
}

//
static void StopApplication( void )
{
	FrmCloseAllForms();
	PanelFreeData();
}

//
static void PanelPickListDrawItem( UInt itemNum, RectanglePtr bounds, CharPtr* unusedP )
{
	CharPtr itemText;

	CALLBACK_PROLOGUE

	itemText = panelIDsP[itemNum].name;
	WinDrawChars( itemText, StrLen(itemText), bounds->topLeft.x, bounds->topLeft.y );

	CALLBACK_EPILOGUE
}

//
static void CreatePanelPickList( ControlPtr triggerP, ListPtr listP, ListDrawDataFuncPtr func )
{
	int item;
	UInt currentAppCardNo;
	LocalID currentAppDBID;

	SysCreatePanelList( &panelCount, &panelIDsH );

	if( panelCount > 0 ){
		panelIDsP = MemHandleLock( (VoidHand)panelIDsH );
	}

	LstSetListChoices( listP, NULL, panelCount );

	LstSetHeight( listP, panelCount );

	LstSetDrawFunction( listP, func );

	SysCurAppDatabase( &currentAppCardNo, &currentAppDBID );
	for( item = 0; item < panelCount; item++ ){
		if( (panelIDsP[item].dbID == currentAppDBID) && (panelIDsP[item].cardNo == currentAppCardNo) ){
			 LstSetSelection( listP, item );
			 CtlSetLabel( triggerP, panelIDsP[item].name );
			 break;
		 }
	}
}

/*
//
static void UpdateMainFormMemory( FormPtr formP, Word cardNo )
{
	DWord RomSize;
	DWord RamSize;
	DWord FreeBytes;
	DWord dwTemp;
	Word wNum10, wNum01;
	Char CardName[32];
	Char szBuf[32];
	Char szBuf2[32];

	if( MemCardInfo( 0, CardName, 0, 0, 0, &RomSize, &RamSize, &FreeBytes ) == 0 ){
		if( RamSize == 0 && RomSize > 0 ){
			StrCopy( szBuf, "ROM " );
			StrIToA( szBuf2, RomSize / 1024 );
			StrCat( szBuf2, " K" );
			StrCat( szBuf, szBuf2 );
		} else {
			// StrIToA( szBuf, FreeBytes / 1024 );
			// StrCat( szBuf, " K / " );
			dwTemp = FreeBytes;
			dwTemp /= (DWord)1024;
			dwTemp *= (DWord)10L;
			dwTemp /= (DWord)1024;
			wNum10 = (Word)dwTemp / 10;
			wNum01 = (Word)dwTemp - (wNum10 * 10);
			szBuf[0] = '0' + wNum10;
			szBuf[1] = '.';
			szBuf[2] = '0' + wNum01;
			szBuf[3] = 0;
			StrCat( szBuf, " M / " );
			// StrIToA( szBuf2, RamSize / 1024 );
			// StrCat( szBuf2, " K" );
			dwTemp = RamSize;
			dwTemp /= (DWord)1024;
			dwTemp *= (DWord)10L;
			dwTemp /= (DWord)1024;
			wNum10 = (Word)dwTemp / 10;
			wNum01 = (Word)dwTemp - (wNum10 * 10);
			szBuf2[0] = '0' + wNum10;
			szBuf2[1] = '.';
			szBuf2[2] = '0' + wNum01;
			szBuf2[3] = 0;
			StrCat( szBuf2, " M" );
			StrCat( szBuf, szBuf2 );
		}
	} else {
		StrCopy( szBuf, "MemCardInfo Error" );
	}

	MySetStrToField( fieldMemSize, szBuf );
}
*/

//
static void UpdateMainFormDepth( FormPtr formP, DWord romVersion )
{
	DWord depth;
	Char szBuf[32];

	depth = 0;

	if( sysGetROMVerMajor(romVersion) >= 3 ){
		if( ScrDisplayMode( scrDisplayModeGet, NULL, NULL, &depth, NULL ) != 0 ){
			depth = 0;
		}
	}

	if( depth > 0 ){
		FrmShowObject( formP, FrmGetObjectIndex( formP, fieldScrDepth ));
		FrmShowObject( formP, FrmGetObjectIndex( formP, labelScrDepth ));
		StrIToA( szBuf, depth );
		StrCat( szBuf, " Bit" );
		MySetStrToField( fieldScrDepth, szBuf );
	}
}

//
static void UpdateMainForm( FormPtr formP )
{
	Word clk;
	DWord romVersion;
	DWord productID;
	Char szBuf[32];
	Char szBuf2[32];
	// SWord wx ,wy;
	UInt voltage, warning, critical;
	SysBatteryKind btype;

	// CPU
	StrCopy( szBuf, "unknown" );
	switch( GetProcessor() ){
		case 'N':	StrCopy( szBuf, "68328"  );		break;
		case 'E':	StrCopy( szBuf, "68EZ328" );	break;
		case 'V':	StrCopy( szBuf, "68VZ328" );	break;
		case '?':	StrCopy( szBuf, "error" );		break;
	}
	MySetStrToField( fieldProcessor, szBuf );

	clk = FreqToClock( GetFreq() );
	StrIToA( szBuf, clk );
	StrCat( szBuf, "MHz" );
	MySetStrToField( fieldCPUClock, szBuf );

	// DRAM
	switch( GetProcessor() ){
	case 'E':
	case 'V':
		FrmShowObject( formP, FrmGetObjectIndex( formP, buttonDramFast ));
		FrmShowObject( formP, FrmGetObjectIndex( formP, buttonDramSlow ));
		FrmShowObject( formP, FrmGetObjectIndex( formP, labelDramWait ));
		FrmShowObject( formP, FrmGetObjectIndex( formP, checkDramReset ));
		FrmSetControlGroupSelection( formP, 1, GetDramNoWait() ? buttonDramFast : buttonDramSlow );
		FrmSetControlValue( formP, FrmGetObjectIndex( formP, checkDramReset ), prefs.swResetDram );
		break;
	}

	// WinDrawLine( 5, 70, 155, 70 );

	// Version
	FtrGet( sysFtrCreator, sysFtrNumROMVersion, &romVersion );
	/*
	StrCopy( szBuf, "" );
	StrIToA( szBuf2, sysGetROMVerMajor( romVersion ));
	StrCat( szBuf, szBuf2 );
	StrCat( szBuf, "." );
	StrIToA( szBuf2, sysGetROMVerMinor( romVersion ));
	StrCat( szBuf, szBuf2 );
	StrCat( szBuf, "." );
	StrIToA( szBuf2, sysGetROMVerFix( romVersion ));
	StrCat( szBuf, szBuf2 );
	*/
		/*
			StrCat( szBuf, "." );
			StrIToA( szBuf2, sysGetROMVerStage( romVersion ));
			StrCat( szBuf, szBuf2 );
			StrCat( szBuf, "." );
			StrIToA( szBuf2, sysGetROMVerBuild( romVersion ));
			StrCat( szBuf, szBuf2 );
		*/
	// MySetStrToField( fieldPalmOSVer, szBuf );

	StrIToH( szBuf, romVersion );
	// MySetStrToField( fieldPalmOSVer, szBuf );

	// ProductID
	FtrGet( sysFtrCreator, sysFtrNumProductID, &productID );
	StrIToH( szBuf2, productID );

	StrCat( szBuf, ", " );
	StrCat( szBuf, szBuf2 );
	MySetStrToField( fieldPalmOSVer, szBuf );

	// UpdateMainFormMemory( formP, 0 );

	UpdateMainFormDepth( formP, romVersion );

/*
	WinGetDisplayExtent( &wx, &wy );
	StrCopy( szBuf, "" );
	StrIToA( szBuf, wx );
	StrCat( szBuf, ", " );
	StrIToA( szBuf2, wy );
	StrCat( szBuf, szBuf2 );
	MySetStrToField( fieldScrSize, szBuf );
*/

	StrCopy( szBuf, "" );
	voltage = SysBatteryInfo( false, &warning, &critical, NULL, &btype, NULL );
	szBuf2[0] = '0' + ( voltage / 100 );
	szBuf2[1] = '.';
	szBuf2[2] = '0' + ( voltage / 10 - (( voltage / 100 ) * 10 ));
	szBuf2[3] = '0' + ( voltage      - (( voltage / 10  ) * 10 ));
	szBuf2[4] = 'v';
	szBuf2[5] = ' ';
	szBuf2[6] = 0;
	StrCat( szBuf, szBuf2 );
	switch( btype ){
		case 0:		StrCopy( szBuf2, "Alkaline" );	break;
		case 1:		StrCopy( szBuf2, "NiCad" );		break;
		case 2:		StrCopy( szBuf2, "Lithium" );	break;
		case 3:		StrCopy( szBuf2, "Recharge" );	break;
		case 4:		StrCopy( szBuf2, "NiMH" );		break;
		case 5:		StrCopy( szBuf2, "LiIon" );		break;
		default:	StrIToA( szBuf2, btype );		break;
	};
	StrCat( szBuf, szBuf2 );
	MySetStrToField( fieldBattery, szBuf );

	szBuf[0]  = '0' + ( warning / 100 );
	szBuf[1]  = '.';
	szBuf[2]  = '0' + ( warning / 10 - (( warning / 100 ) * 10 ));
	szBuf[3]  = '0' + ( warning      - (( warning / 10  ) * 10 ));
	szBuf[4]  = 'v';
	szBuf[5]  = ' ';
	szBuf[6]  = '/';
	szBuf[7]  = ' ';
	szBuf[8]  = '0' + ( critical / 100 );
	szBuf[9]  = '.';
	szBuf[10] = '0' + ( critical / 10 - (( critical / 100 ) * 10 ));
	szBuf[11] = '0' + ( critical      - (( critical / 10  ) * 10 ));
	szBuf[12] = 'v';
	szBuf[13] = 0;
	MySetStrToField( fieldBatWarn, szBuf );
}

//
static void MainFormInit( FormPtr formP )
{
	RectangleType r;
	Word distance;
	Word doneButtonIndex;

	if( CalledFromAppMode ){
		FrmHideObject( formP, FrmGetObjectIndex( formP, mainPanelPickTrigger ));
		FrmShowObject( formP, FrmGetObjectIndex( formP, mainPanelNameLabel ));
		doneButtonIndex = FrmGetObjectIndex( formP, mainDoneButton );
		FrmShowObject( formP, doneButtonIndex );
		FrmGetObjectBounds( formP, doneButtonIndex, &r );
		distance = r.extent.x + 6;
		// MoveObject( formP, FirstButton, distance, 0, false );
		// MoveObject( formP, SecondButton, distance, 0, false );
	}
}

//
static Boolean MainFormHandleEvent( EventPtr event )
{
	FormPtr formP;
	UInt currentAppCardNo;
	LocalID currentAppDBID;
	EventType newEvent;
	PrefActivePanelParamsType prefsActive;
	Boolean handled = false;

	CALLBACK_PROLOGUE

	switch( event->eType ){
	case frmOpenEvent:
		LoadPrefs( &prefs );
		formP = FrmGetActiveForm();
		MainFormInit( formP );
		FrmDrawForm( formP );
		UpdateMainForm( formP );
		if( ! CalledFromAppMode ){
			CreatePanelPickList(
				FrmGetObjectPtr( formP, FrmGetObjectIndex( formP, mainPanelPickTrigger )),
				FrmGetObjectPtr( formP, FrmGetObjectIndex( formP, mainPanelPickList )), PanelPickListDrawItem );
			FrmShowObject( formP, FrmGetObjectIndex( formP, mainPanelPickTrigger ));
			prefsActive.activePanel = Creator;
			AppCallWithCommand( sysFileCPreferences, prefAppLaunchCmdSetActivePanel, &prefsActive );
		}
		handled = true;
		break;
	case frmCloseEvent:
		PanelFreeData();
		SavePrefs( &prefs );
		break;
	case popSelectEvent:
		switch( event->data.popSelect.listID ){
		case mainPanelPickList:
			SysCurAppDatabase( &currentAppCardNo, &currentAppDBID );
			if( panelIDsP[event->data.popSelect.selection].dbID != currentAppDBID ||
				panelIDsP[event->data.popSelect.selection].cardNo != currentAppCardNo )
			{
				SysUIAppSwitch( panelIDsP[event->data.popSelect.selection].cardNo,
								panelIDsP[event->data.popSelect.selection].dbID, sysAppLaunchCmdNormalLaunch, 0 );
			}
			handled = true;
			break;
		}
		break;
	case menuEvent:
		switch( event->data.menu.itemID ){
		case menuAbout:
			// FrmAlert( alertAboutInformation );
			FrmHelp( helpAbout );
			handled = true;
			break;
		case menuRefresh:
			UpdateMainForm( FrmGetActiveForm() );
			handled = true;
			break;
		}
		break;
	case ctlSelectEvent:
		switch( event->data.ctlSelect.controlID ){
		case mainDoneButton:
			newEvent.eType = appStopEvent;
			EvtAddEventToQueue( &newEvent );
			handled = true;
			break;
		case buttonDramFast:
			switch( GetProcessor() ){
			case 'V':
			case 'E':
				prefs.fDramNoWait = true;
				SetDramNoWait( true );
				break;
			}
			handled = true;
			break;
		case buttonDramSlow:
			switch( GetProcessor() ){
			case 'V':
			case 'E':
				prefs.fDramNoWait = false;
				SetDramNoWait( false );
				break;
			}
			handled = true;
			break;
		case checkDramReset:
			formP = FrmGetActiveForm();
			prefs.swResetDram = FrmGetControlValue( formP, FrmGetObjectIndex( formP, checkDramReset ));
			handled = true;
			break;
		}
		break;
//	case nilEvent:
	//	handled = true;
	//	break;
	default:
		break;
	}

	CALLBACK_EPILOGUE

	return( handled );
}

//
static Boolean ApplicationHandleEvent( EventPtr event )
{
	FormPtr formP;
	Word formId;

	if( event->eType == frmLoadEvent ){
		formId = event->data.frmLoad.formID;
		formP = FrmInitForm( formId );
		FrmSetActiveForm( formP );
		switch( formId ){
		case mainForm:
			FrmSetEventHandler( formP, MainFormHandleEvent );
			break;
		}
		return( true );
	}

	return( false );
}

//
static void EventLoop( void )
{
	EventType event;
	Err error;

	do {
		EvtGetEvent( &event, sysTicksPerSecond );
		if( ! SysHandleEvent( &event ))
			if( ! MenuHandleEvent( NULL, &event, &error ))
				if( ! ApplicationHandleEvent( &event ))
					FrmDispatchEvent( &event );
	} while( event.eType != appStopEvent );
}

//
/*
static Word RomVersionCompatible( DWord requiredVersion, Word launchFlags )
{
	DWord romVersion;

	FtrGet( sysFtrCreator, sysFtrNumROMVersion, &romVersion );
	if( romVersion < requiredVersion ){
		if( (launchFlags & (sysAppLaunchFlagNewGlobals | sysAppLaunchFlagUIApp)) == 
			(sysAppLaunchFlagNewGlobals | sysAppLaunchFlagUIApp) )
		{
			FrmAlert( alertRomIncompatible );
			if( romVersion < 0x02000000 ){
				AppLaunchWithCommand( sysFileCDefaultApp, sysAppLaunchCmdNormalLaunch, NULL );
			}
		}
		return( sysErrRomIncompatible );
	}

	return( 0 );
}
*/

//
DWord PilotMain( Word cmd, Ptr cmdPBP, Word launchFlags )
{
	Word error;

	// error = RomVersionCompatible( 0x03000000, launchFlags );
	//	if( error )
	//		return( error );

	switch( cmd ){
	case sysAppLaunchCmdNormalLaunch:
	case sysAppLaunchCmdPanelCalledFromApp:
	case sysAppLaunchCmdReturnFromPanel:
		CalledFromAppMode = ( cmd == sysAppLaunchCmdPanelCalledFromApp );
		error = StartApplication();
		FrmGotoForm( mainForm );
		if( ! error ){
			EventLoop();
		}
		StopApplication();
		break;
	case sysAppLaunchCmdSystemReset:
		LoadPrefs( &prefs );
		break;
	default:
		break;
	}

	return( 0 );
}

// end
