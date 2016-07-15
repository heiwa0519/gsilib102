/*------------------------------------------------------------------------------
* keydlg.cpp
*
*    Copyright (C) 2014 by Geospatial Information Authority of Japan,
*    All rights reserved.
*
*
*  Original software: RTKLIB ver.2.4.2 p4
*
*    Copyright (C) 2007-2013 by T.Takasu, All rights reserved.
*
*
* references :
*
* history : 2015/01/08  1.0  new
*-----------------------------------------------------------------------------*/

#include <vcl.h>
#pragma hdrstop

#include "keydlg.h"
//---------------------------------------------------------------------------
#pragma package(smart_init)
#pragma resource "*.dfm"
TKeyDialog *KeyDialog;
//---------------------------------------------------------------------------
__fastcall TKeyDialog::TKeyDialog(TComponent* Owner)
	: TForm(Owner)
{
	Flag=0;
}
//---------------------------------------------------------------------------
void __fastcall TKeyDialog::FormShow(TObject *Sender)
{
	Label10->Visible=Flag!=3;
	Label21->Visible=Flag!=3;
	Label23->Visible=Flag!=3;
	Label24->Visible=Flag!=3;
	Label25->Visible=Flag!=3;
	Label26->Visible=Flag!=3;
	Label27->Visible=Flag!=3;
	Label28->Visible=Flag!=3;
	Label29->Visible=Flag>=1;
	Label30->Visible=Flag>=1;
	Label31->Visible=Flag==2;
	Label32->Visible=Flag==2;
	Label33->Visible=Flag==3;
	Label34->Visible=Flag==3;
	Label35->Visible=Flag==3;
	Label36->Visible=Flag==3;
	Label37->Visible=Flag==3;
	Label38->Visible=Flag==3;
}
//---------------------------------------------------------------------------
void __fastcall TKeyDialog::BtnOkClick(TObject *Sender)
{
	Close();
}
//---------------------------------------------------------------------------

