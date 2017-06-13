﻿//
// MainPage.xaml.cpp
// MainPage 类的实现。
//

#include "pch.h"
#include "MainPage.xaml.h"
#include "DownloadDialog.xaml.h"
#include "StringHelper.h"
#include "AsyncHelper.h"
#include "global.h"
#include "palcfg.h"
#include "util.h"
#include "generated.h"

using namespace SDLPal;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::UI::Popups;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;

static Platform::String^ msg_file_exts[] = { ".msg" };
static Platform::String^ font_file_exts[] = { ".bdf" };
static Platform::String^ log_file_exts[] = { ".log" };

MainPage^ MainPage::Current = nullptr;

MainPage::MainPage()
{
	InitializeComponent();

	Current = this;

	m_controls = ref new Platform::Collections::Map<Platform::String^, ButtonAttribute^>();
	m_controls->Insert(btnBrowseMsgFile->Name, ref new ButtonAttribute(tbMsgFile, ref new Platform::Array<Platform::String^>(msg_file_exts, sizeof(msg_file_exts) / sizeof(msg_file_exts[0]))));
	m_controls->Insert(btnBrowseFontFile->Name, ref new ButtonAttribute(tbFontFile, ref new Platform::Array<Platform::String^>(font_file_exts, sizeof(font_file_exts) / sizeof(font_file_exts[0]))));
	m_controls->Insert(btnBrowseLogFile->Name, ref new ButtonAttribute(tbLogFile, ref new Platform::Array<Platform::String^>(log_file_exts, sizeof(log_file_exts) / sizeof(log_file_exts[0]))));
	m_controls->Insert(cbUseMsgFile->Name, ref new ButtonAttribute(gridMsgFile, nullptr));
	m_controls->Insert(cbUseFontFile->Name, ref new ButtonAttribute(gridFontFile, nullptr));
	m_controls->Insert(cbUseLogFile->Name, ref new ButtonAttribute(gridLogFile, nullptr));

	m_acl[PALCFG_GAMEPATH] = ref new AccessListEntry(tbGamePath, nullptr, ConvertString(PAL_ConfigName(PALCFG_GAMEPATH)));
	m_acl[PALCFG_SAVEPATH] = ref new AccessListEntry(tbGamePath, nullptr, ConvertString(PAL_ConfigName(PALCFG_SAVEPATH)));
	m_acl[PALCFG_MESSAGEFILE] = ref new AccessListEntry(tbMsgFile, cbUseMsgFile, ConvertString(PAL_ConfigName(PALCFG_MESSAGEFILE)));
	m_acl[PALCFG_FONTFILE] = ref new AccessListEntry(tbFontFile, cbUseFontFile, ConvertString(PAL_ConfigName(PALCFG_FONTFILE)));
	m_acl[PALCFG_LOGFILE] = ref new AccessListEntry(tbLogFile, cbUseLogFile, ConvertString(PAL_ConfigName(PALCFG_LOGFILE)));

	tbGitRevision->Text = "  " PAL_GIT_REVISION;

	LoadControlContents(false);

	btnDownloadGame->IsEnabled = (tbGamePath->Text->Length() > 0);

	RadioButton^ links[] = { rbDownloadLink1, rbDownloadLink2, rbDownloadLink3 };
	srand(time(NULL));
	links[rand() % 3]->IsChecked = true;

	m_resLdr = Windows::ApplicationModel::Resources::ResourceLoader::GetForCurrentView();
	if (static_cast<App^>(Application::Current)->LastCrashed)
	{
		(ref new MessageDialog(m_resLdr->GetString("MBCrashContent")))->ShowAsync();
	}

	try
	{
		delete AWait(Windows::Storage::ApplicationData::Current->LocalFolder->GetFileAsync("sdlpal.cfg"));
	}
	catch (Exception^)
	{
		(ref new MessageDialog(m_resLdr->GetString("MBStartupMessage"), m_resLdr->GetString("MBStartupTitle")))->ShowAsync();
	}
}

void SDLPal::MainPage::LoadControlContents(bool loadDefault)
{
	for (auto i = m_acl.begin(); i != m_acl.end(); i++)
	{
		auto item = i->second;
		item->text->Text = "";
		item->text->Tag = nullptr;
		if (item->check)
		{
			item->check->IsChecked = false;
			m_controls->Lookup(item->check->Name)->Object->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
		}
	}

	if (!loadDefault)
	{
		// Always load folder/files from FutureAccessList
		std::list<Platform::String^> invalid_tokens;
		auto fal = Windows::Storage::AccessCache::StorageApplicationPermissions::FutureAccessList;
		for each (auto entry in fal->Entries)
		{
			auto& ace = m_acl[PAL_ConfigIndex(ConvertString(entry.Token).c_str())];
			ace->text->Tag = AWait(fal->GetItemAsync(entry.Token), g_eventHandle);
			if (ace->text->Tag)
				ace->text->Text = entry.Metadata;
			else
				invalid_tokens.push_back(entry.Token);
			if (ace->check)
			{
				auto grid = m_controls->Lookup(ace->check->Name)->Object;
				ace->check->IsChecked = (ace->text->Tag != nullptr);
				grid->Visibility = ace->check->IsChecked->Value ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
			}
		}
		for (auto i = invalid_tokens.begin(); i != invalid_tokens.end(); fal->Remove(*i++));
	}

	tsKeepAspect->IsOn = (gConfig.fKeepAspectRatio == TRUE);
	tsStereo->IsOn = (gConfig.iAudioChannels == 2);
	tsSurroundOPL->IsOn = (gConfig.fUseSurroundOPL == TRUE);
	tsTouchOverlay->IsOn = (gConfig.fUseTouchOverlay == TRUE);
	tsEnableAVI->IsOn = (gConfig.fEnableAviPlay == TRUE);

	slMusicVolume->Value = gConfig.iMusicVolume;
	slSoundVolume->Value = gConfig.iSoundVolume;
	slQuality->Value = gConfig.iResampleQuality;
	cbLogLevel->SelectedIndex = (int)gConfig.iLogLevel;

	cbCD->SelectedIndex = (gConfig.eCDType == MUSIC_MP3) ? 0 : 1;
	cbBGM->SelectedIndex = (gConfig.eMusicType <= MUSIC_OGG) ? gConfig.eMusicType : MUSIC_RIX;
	cbOPL->SelectedIndex = (int)gConfig.eOPLType;

	if (gConfig.iSampleRate <= 11025)
		cbSampleRate->SelectedIndex = 0;
	else if (gConfig.iSampleRate <= 22050)
		cbSampleRate->SelectedIndex = 1;
	else
		cbSampleRate->SelectedIndex = 2;

	auto wValue = gConfig.wAudioBufferSize >> 10;
	unsigned int index = 0;
	while (wValue) { index++; wValue >>= 1; }
	if (index >= cbAudioBuffer->Items->Size)
		cbAudioBuffer->SelectedIndex = cbAudioBuffer->Items->Size - 1;
	else
		cbAudioBuffer->SelectedIndex = index;

	if (gConfig.iOPLSampleRate <= 12429)
		cbOPLSR->SelectedIndex = 0;
	else if (gConfig.iSampleRate <= 24858)
		cbOPLSR->SelectedIndex = 1;
	else
		cbOPLSR->SelectedIndex = 2;
}

void SDLPal::MainPage::SaveControlContents()
{
	// All folders/files are not stored in config file, as they are store in FutureAcessList
	if (gConfig.pszGamePath) { free(gConfig.pszGamePath); gConfig.pszGamePath = nullptr; }
	if (gConfig.pszMsgFile) { free(gConfig.pszMsgFile); gConfig.pszMsgFile = nullptr; }
	if (gConfig.pszFontFile) { free(gConfig.pszFontFile); gConfig.pszFontFile = nullptr; }
	if (gConfig.pszLogFile) { free(gConfig.pszLogFile); gConfig.pszLogFile = nullptr; }

	gConfig.fKeepAspectRatio = tsKeepAspect->IsOn ? TRUE : FALSE;
	gConfig.iAudioChannels = tsStereo->IsOn ? 2 : 1;
	gConfig.fUseSurroundOPL = tsSurroundOPL->IsOn ? TRUE : FALSE;
	gConfig.fUseTouchOverlay = tsTouchOverlay->IsOn ? TRUE : FALSE;
	gConfig.fEnableAviPlay = tsEnableAVI->IsOn ? TRUE : FALSE;

	gConfig.iMusicVolume = (int)slMusicVolume->Value;
	gConfig.iSoundVolume = (int)slSoundVolume->Value;
	gConfig.iResampleQuality = (int)slQuality->Value;
	gConfig.iLogLevel = (LOGLEVEL)cbLogLevel->SelectedIndex;

	gConfig.eCDType = (MUSICTYPE)(MUSIC_MP3 + cbCD->SelectedIndex);
	gConfig.eMusicType = (MUSICTYPE)cbBGM->SelectedIndex;
	gConfig.eOPLType = (OPLTYPE)cbOPL->SelectedIndex;

	gConfig.iSampleRate = wcstoul(static_cast<Platform::String^>(static_cast<ComboBoxItem^>(cbSampleRate->SelectedItem)->Content)->Data(), nullptr, 10);
	gConfig.iOPLSampleRate = wcstoul(static_cast<Platform::String^>(static_cast<ComboBoxItem^>(cbOPLSR->SelectedItem)->Content)->Data(), nullptr, 10);
	gConfig.wAudioBufferSize = wcstoul(static_cast<Platform::String^>(static_cast<ComboBoxItem^>(cbAudioBuffer->SelectedItem)->Content)->Data(), nullptr, 10);
}

void SDLPal::MainPage::cbBGM_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e)
{
	auto visibility = (cbBGM->SelectedIndex == MUSIC_RIX) ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
	cbOPL->Visibility = visibility;
	cbOPLSR->Visibility = visibility;
	tsSurroundOPL->Visibility = visibility;
}

void SDLPal::MainPage::btnDefault_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	PAL_LoadConfig(FALSE);
	LoadControlContents(true);
}

void SDLPal::MainPage::btnReset_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	PAL_LoadConfig(TRUE);
	LoadControlContents(false);
}

void SDLPal::MainPage::btnFinish_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	if (tbGamePath->Text->Length() > 0)
	{
		if (PAL_MISSING_REQUIRED(UTIL_CheckResourceFiles(ConvertString(tbGamePath->Text).c_str(), ConvertString(tbMsgFile->Text).c_str())))
		{
			auto msg = std::wstring(m_resLdr->GetString("MBRequired")->Data());
			msg.replace(msg.find(L"{0}", 0), 3, tbGamePath->Text->Data());
			(ref new MessageDialog(ref new Platform::String(msg.c_str())))->ShowAsync();
			tbGamePath->Focus(Windows::UI::Xaml::FocusState::Programmatic);
			return;
		}

		auto fal = Windows::Storage::AccessCache::StorageApplicationPermissions::FutureAccessList;
		for (auto i = m_acl.begin(); i != m_acl.end(); i++)
		{
			auto item = i->second;
			auto check = item->check ? item->check->IsChecked->Value : true;
			if (check && item->text->Tag)
				fal->AddOrReplace(item->token, safe_cast<Windows::Storage::IStorageItem^>(item->text->Tag), item->text->Text);
			else if (fal->ContainsItem(item->token))
				fal->Remove(item->token);
		}

		SaveControlContents();
		gConfig.fLaunchSetting = FALSE;
		PAL_SaveConfig();

		concurrency::create_task((ref new MessageDialog(m_resLdr->GetString("MBExitContent"), m_resLdr->GetString("MBExitTitle")))->ShowAsync()).then([] (IUICommand^ command) {
			Application::Current->Exit();
		});
	}
	else
	{
		(ref new MessageDialog(m_resLdr->GetString("MBEmptyContent")))->ShowAsync();
	}
}

void SDLPal::MainPage::btnClearFile_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	tbMsgFile->Text = "";
	tbMsgFile->Tag = nullptr;
}

void SDLPal::MainPage::SetPath(Windows::Storage::StorageFolder^ folder)
{
	if (folder)
	{
		tbGamePath->Text = folder->Path;
		tbGamePath->Tag = folder;
		btnDownloadGame->IsEnabled = true;
	}
}

void SDLPal::MainPage::SetFile(Windows::UI::Xaml::Controls::TextBox^ target, Windows::Storage::StorageFile^ file)
{
	if (target && file)
	{
		target->Text = file->Path;
		target->Tag = file;
	}
}

void SDLPal::MainPage::btnBrowseFolder_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	auto picker = ref new Windows::Storage::Pickers::FolderPicker();
	picker->FileTypeFilter->Append("*");
#if WINAPI_FAMILY == WINAPI_FAMILY_PHONE_APP
	picker->PickFolderAndContinue();
#else
	concurrency::create_task(picker->PickSingleFolderAsync()).then([this](Windows::Storage::StorageFolder^ folder) { SetPath(folder); });
#endif
}

void SDLPal::MainPage::btnBrowseFileOpen_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	auto button = static_cast<Windows::UI::Xaml::Controls::Button^>(sender);
	auto target = m_controls->Lookup(button->Name);
	auto picker = ref new Windows::Storage::Pickers::FileOpenPicker();
	picker->FileTypeFilter->ReplaceAll(target->Filter);
#if WINAPI_FAMILY == WINAPI_FAMILY_PHONE_APP
	picker->ContinuationData->Insert("Target", button->Name);
	picker->PickSingleFileAndContinue();
#else
	concurrency::create_task(picker->PickSingleFileAsync()).then(
		[this, target](Windows::Storage::StorageFile^ file) {
			SetFile(static_cast<Windows::UI::Xaml::Controls::TextBox^>(target->Object), file);
		}
	);
#endif
}

void SDLPal::MainPage::btnBrowseFileSave_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	auto button = static_cast<Windows::UI::Xaml::Controls::Button^>(sender);
	auto target = m_controls->Lookup(button->Name);
	auto picker = ref new Windows::Storage::Pickers::FileSavePicker();
	picker->FileTypeChoices->Insert(m_resLdr->GetString("LogFileType"), ref new Platform::Collections::Vector<Platform::String^>(target->Filter));
#if WINAPI_FAMILY == WINAPI_FAMILY_PHONE_APP
	picker->ContinuationData->Insert("Target", button->Name);
	picker->PickSaveFileAndContinue();
#else
	concurrency::create_task(picker->PickSaveFileAsync()).then(
		[this, target](Windows::Storage::StorageFile^ file) {
		SetFile(static_cast<Windows::UI::Xaml::Controls::TextBox^>(target->Object), file);
	}
	);
#endif
}

void SDLPal::MainPage::cbUseFile_CheckChanged(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	auto checker = static_cast<Windows::UI::Xaml::Controls::CheckBox^>(sender);
	auto attr = m_controls->Lookup(checker->Name);
	attr->Object->Visibility = checker->IsChecked->Value ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
}

void SDLPal::MainPage::Page_Loaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
#if NTDDI_VERSION >= NTDDI_WIN10
	if (!Windows::Foundation::Metadata::ApiInformation::IsTypePresent("Windows.UI.ViewManagement.StatusBar")) return;
#endif
#if NTDDI_VERSION >= NTDDI_WIN10 || WINAPI_FAMILY == WINAPI_FAMILY_PHONE_APP
	auto statusBar = Windows::UI::ViewManagement::StatusBar::GetForCurrentView();
	concurrency::create_task(statusBar->ShowAsync()).then([statusBar]() { statusBar->BackgroundOpacity = 1.0; });
#endif
}


void SDLPal::MainPage::btnDownloadGame_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	Platform::String^ link = nullptr;
	Windows::UI::Xaml::Controls::RadioButton^ controls[] = {
		this->rbDownloadLink1, this->rbDownloadLink2, this->rbDownloadLink3
	};
	for (int i = 0; i < 3; i++)
	{
		if (controls[i]->IsChecked->Value)
		{
			link = static_cast<Platform::String^>(controls[i]->Tag);
			break;
		}
	}
	auto folder = dynamic_cast<Windows::Storage::StorageFolder^>(tbGamePath->Tag);
	auto msgbox = ref new MessageDialog(m_resLdr->GetString("MBDownloadMessage"), m_resLdr->GetString("MBDownloadTitle"));
	msgbox->Commands->Append(ref new UICommand(m_resLdr->GetString("MBButtonOK"), nullptr, 1));
	msgbox->Commands->Append(ref new UICommand(m_resLdr->GetString("MBButtonCancel"), nullptr, nullptr));
	msgbox->DefaultCommandIndex = 0;
	msgbox->CancelCommandIndex = 1;
	concurrency::create_task(msgbox->ShowAsync()).then([this](IUICommand^ command)->IAsyncOperation<IUICommand^>^ {
		if (command->Id != nullptr)
		{
			if (UTIL_CheckResourceFiles(ConvertString(tbGamePath->Text).c_str(), ConvertString(tbMsgFile->Text).c_str()) != PALFILE_ALL_ORIGIN)
			{
				auto msgbox = ref new MessageDialog(m_resLdr->GetString("MBDownloadOverwrite"), m_resLdr->GetString("MBDownloadTitle"));
				msgbox->Commands->Append(ref new UICommand(m_resLdr->GetString("MBButtonYes"), nullptr, 1));
				msgbox->Commands->Append(ref new UICommand(m_resLdr->GetString("MBButtonNo"), nullptr, nullptr));
				msgbox->DefaultCommandIndex = 0;
				msgbox->CancelCommandIndex = 1;
				return msgbox->ShowAsync();
			}
			else
			{
				return concurrency::create_async([command]()->IUICommand^ { return command; });
			}
		}
		else
		{
			return concurrency::create_async([command]()->IUICommand^ { return command; });
		}
	}).then([this, folder, link](IUICommand^ command) {
		if (command->Id != nullptr)
		{
			HANDLE hEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
			try
			{
				auto file = AWait(folder->CreateFileAsync("pal98.zip", Windows::Storage::CreationCollisionOption::ReplaceExisting), hEvent);
				auto stream = AWait(file->OpenAsync(Windows::Storage::FileAccessMode::ReadWrite), hEvent);
				auto dlg = ref new DownloadDialog(link, m_resLdr, folder, stream);
				dlg->MinWidth = ActualWidth;
				dlg->MinHeight = ActualHeight;
				concurrency::create_task(dlg->ShowAsync()).then(
					[this, file, stream, hEvent](ContentDialogResult result) {
					delete stream;
					AWait(file->DeleteAsync(), hEvent);
					delete file;
					CloseHandle(hEvent);
				});
			}
			catch (Exception^ e)
			{
				(ref new MessageDialog(String::Concat(m_resLdr->GetString("MBDownloadError"), e)))->ShowAsync();
				CloseHandle(hEvent);
			}
		}
	});
}
