﻿//
// DownloadDialog.xaml.cpp
// DownloadDialog 类的实现
//

#include "pch.h"
#include <wrl.h>
#include <shcore.h>
#include "DownloadDialog.xaml.h"
#include "AsyncHelper.h"
#include "NativeBuffer.h"
#include "StringHelper.h"
#include "minizip/unzip.h"

using namespace SDLPal;

using namespace Platform;
using namespace Platform::Collections;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Storage;
using namespace Windows::Storage::Compression;
using namespace Windows::Storage::Streams;
using namespace Windows::UI::Core;
using namespace Windows::UI::Popups;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;
using namespace Windows::Web::Http;

// https://go.microsoft.com/fwlink/?LinkId=234238 上介绍了“内容对话框”项模板

static const uint32_t _buffer_size = 65536;
static Platform::String^ const _url = "http://pal5q.baiyou100.com/pal5/download/98xjrq.html";
static const wchar_t _postfix[] = L"/Pal98rqp.zip";

struct zip_file
{
	IStream* stream;
	HRESULT  hr;
	ULONG    cbBytes;

	zip_file(IStream* s) : stream(s), hr(S_OK), cbBytes(0) {}
};

SDLPal::DownloadDialog::DownloadDialog(Platform::String^ link, Windows::ApplicationModel::Resources::ResourceLoader^ ldr, Windows::Storage::StorageFolder^ folder, Windows::Storage::Streams::IRandomAccessStream^ stream)
	: m_link(link), m_stream(stream), m_Closable(false), m_InitialPhase(true), m_totalBytes(0), m_resLdr(ldr), m_folder(folder)
{
	InitializeComponent();

	this->IsSecondaryButtonEnabled = false;
}

Platform::String^ SDLPal::DownloadDialog::FormatProgress()
{
	static auto format = [](wchar_t* buf, double v)->wchar_t* {
		if (v <= 1024.0)
			swprintf_s(buf, 32, L"%0.0f B", v);
		else if (v <= 1048576.0)
			swprintf_s(buf, 32, L"%0.2f KB", v / 1024.0);
		else if (v <= 1073741824.0)
			swprintf_s(buf, 32, L"%0.2f MB", v / 1048576.0);
		else
			swprintf_s(buf, 32, L"%0.2f GB", v / 1073741824.0);
		return buf;
	};

	wchar_t buf[64], buf1[32], buf2[32];
	swprintf_s(buf, L"%s / %s", format(buf1, pbDownload->Value), format(buf2, pbDownload->Maximum));
	return ref new Platform::String(buf);
}

void SDLPal::DownloadDialog::OnPrimaryButtonClick(Windows::UI::Xaml::Controls::ContentDialog^ sender, Windows::UI::Xaml::Controls::ContentDialogButtonClickEventArgs^ args)
{
	m_Closable = true;
}


void SDLPal::DownloadDialog::OnClosing(Windows::UI::Xaml::Controls::ContentDialog^ sender, Windows::UI::Xaml::Controls::ContentDialogClosingEventArgs^ args)
{
	args->Cancel = !m_Closable;
}


void SDLPal::DownloadDialog::OnOpened(Windows::UI::Xaml::Controls::ContentDialog^ sender, Windows::UI::Xaml::Controls::ContentDialogOpenedEventArgs^ args)
{
	pbDownload->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	tbProgress->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	DownloadPage->Visibility = Windows::UI::Xaml::Visibility::Visible;
	DownloadPage->Width = spContent->ActualWidth;
	DownloadPage->Height = 500;
	DownloadPage->Navigate(ref new Uri(_url));
}


void SDLPal::DownloadDialog::OnNavigateStart(Windows::UI::Xaml::Controls::WebView^ sender, Windows::UI::Xaml::Controls::WebViewNavigationStartingEventArgs^ args)
{
	auto url = args->Uri->RawUri;
	args->Cancel = (Platform::String::CompareOrdinal(url, _url) != 0);

	if (url->Length() >= 12 && _wcsicmp(url->Data() + url->Length() - _countof(_postfix), _postfix) == 0)
	{
		pbDownload->Visibility = Windows::UI::Xaml::Visibility::Visible;
		tbProgress->Visibility = Windows::UI::Xaml::Visibility::Visible;
		DownloadPage->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
		DownloadPage->Width = 0;
		DownloadPage->Height = 0;
		this->MinWidth = 0;
		this->MinHeight = 0;
		this->Title = m_resLdr->GetString("Downloading.Title");
		
		concurrency::create_task([this]() {
			Exception^ ex = nullptr;
			auto client = ref new HttpClient();
			try
			{
				concurrency::create_task(client->GetAsync(ref new Uri(m_link), HttpCompletionOption::ResponseHeadersRead)).then(
					[this](HttpResponseMessage^ response)->IAsyncOperationWithProgress<IInputStream^, uint64_t>^ {
					response->EnsureSuccessStatusCode();

					bool determinate = response->Content->Headers->HasKey("Content-Length");
					if (determinate)
					{
						m_totalBytes = wcstoull(response->Content->Headers->Lookup("Content-Length")->Data(), nullptr, 10);
					}
					this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, determinate]() {
						pbDownload->Maximum = (double)m_totalBytes;
						pbDownload->IsIndeterminate = !determinate;
					}));
					return response->Content->ReadAsInputStreamAsync();
				}).then([this, client](IInputStream^ input) {
					auto buffer = ref new Buffer(_buffer_size);
					uint64_t bytes = 0;
					HANDLE hEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
					for (bool looping = true; looping; )
					{
						concurrency::create_task(input->ReadAsync(buffer, _buffer_size, InputStreamOptions::None)).then(
							[this, &bytes, &looping](IBuffer^ result)->IAsyncOperationWithProgress<uint32_t, uint32_t>^ {
							looping = (result->Length == _buffer_size) && !m_Closable;
							bytes += result->Length;
							this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, bytes]() {
								pbDownload->Value = (double)bytes;
								tbProgress->Text = FormatProgress();
								UpdateLayout();
							}));
							return m_stream->WriteAsync(result);
						}).then([this](uint32_t)->IAsyncOperation<bool>^ {
							return m_stream->FlushAsync();
						}).wait();
					}
					delete buffer;
					delete client;
					delete input;

					this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, bytes]() {
						this->Title = m_resLdr->GetString("Extracting");
						pbDownload->Value = 0.0;
						UpdateLayout();
					}));

					m_stream->Seek(0);

					Microsoft::WRL::ComPtr<IStream> strm;
					HRESULT hr;
					if (FAILED(hr = CreateStreamOverRandomAccessStream(m_stream, IID_PPV_ARGS(&strm)))) throw ref new Platform::Exception(hr);

					zlib_filefunc_def funcs = {
						/* open  */ [](voidpf opaque, const char* filename, int mode)->voidpf { return new zip_file((IStream*)opaque); },
						/* read  */ [](voidpf opaque, voidpf stream, void* buf, uLong size)->uLong {
						auto zip = (zip_file*)stream;
						return SUCCEEDED(zip->hr = zip->stream->Read(buf, size, &zip->cbBytes)) ? zip->cbBytes : 0;
					},
						/* write */ [](voidpf opaque, voidpf stream, const void* buf, uLong size)->uLong {
						auto zip = (zip_file*)stream;
						return SUCCEEDED(zip->hr = zip->stream->Write(buf, size, &zip->cbBytes)) ? zip->cbBytes : 0;
					},
						/* tell  */ [](voidpf opaque, voidpf stream)->long {
						auto zip = (zip_file*)stream;
						LARGE_INTEGER liPos = { 0 };
						ULARGE_INTEGER uliPos;
						return SUCCEEDED(zip->hr = zip->stream->Seek(liPos, STREAM_SEEK_CUR, &uliPos)) ? uliPos.LowPart : UNZ_ERRNO;
					},
						/* seek  */ [](voidpf opaque, voidpf stream, uLong offset, int origin)->long {
						auto zip = (zip_file*)stream;
						LARGE_INTEGER liPos = { offset };
						ULARGE_INTEGER uliPos;
						return SUCCEEDED(zip->hr = zip->stream->Seek(liPos, origin, &uliPos)) ? 0 : UNZ_ERRNO;
					},
						/* close */ [](voidpf opaque, voidpf stream)->int { delete (zip_file*)stream; return 0; },
						/* error */ [](voidpf opaque, voidpf stream)->int { return reinterpret_cast<zip_file*>(stream)->hr; },
						strm.Get()
					};
					unz_global_info ugi;
					char szFilename[65536];
					uLong filenum = 0;
					bool success = true;

					auto uzf = unzOpen2("", &funcs);
					unzGetGlobalInfo(uzf, &ugi);
					this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, ugi]() { pbDownload->Maximum = ugi.number_entry; UpdateLayout(); }));
					for (auto ret = unzGoToFirstFile(uzf); ret == UNZ_OK; ret = unzGoToNextFile(uzf))
					{
						unz_file_info ufi;
						if (UNZ_OK == unzGetCurrentFileInfo(uzf, &ufi, szFilename, sizeof(szFilename), nullptr, 0, nullptr, 0) &&
							UNZ_OK == unzOpenCurrentFile(uzf))
						{
							std::auto_ptr<uint8_t> buf(new uint8_t[ufi.uncompressed_size]);
							auto len = unzReadCurrentFile(uzf, buf.get(), ufi.uncompressed_size);
							unzCloseCurrentFile(uzf);
							if (len != ufi.uncompressed_size)
							{
								success = false;
								break;
							}

							auto local = m_folder;
							uLong prev = 0;
							for (uLong i = 0; i < ufi.size_filename; i++)
							{
								if (szFilename[i] != '/') continue;
								try
								{
									concurrency::create_task(local->GetFolderAsync(ConvertString(szFilename + prev, i - prev))).then([&local](StorageFolder^ sub) { local = sub; }).wait();
								}
								catch (Exception^ e)
								{
									concurrency::create_task(local->CreateFolderAsync(ConvertString(szFilename + prev, i - prev))).then([&local](StorageFolder^ sub) { local = sub; }).wait();
								}
								prev = i + 1;
							}
							if (prev < ufi.size_filename)
							{
								IRandomAccessStream^ stm = nullptr;
								StorageFile^ file = nullptr;
								auto filename = ConvertString(szFilename + prev, ufi.size_filename - prev);
								concurrency::create_task(local->CreateFileAsync(filename, CreationCollisionOption::ReplaceExisting)).then([&](StorageFile^ f)->IAsyncOperation<IRandomAccessStream^>^ {
									return (file = f)->OpenAsync(Windows::Storage::FileAccessMode::ReadWrite);
								}).then([&](IRandomAccessStream^ s)->IAsyncOperationWithProgress<uint32_t, uint32_t>^ {
									return (stm = s)->WriteAsync(NativeBuffer::GetIBuffer(buf.get(), ufi.uncompressed_size));
								}).then([&](uint32_t size)->IAsyncOperation<bool>^ {
									if (size < ufi.uncompressed_size) throw ref new Exception(E_FAIL);
									return stm->FlushAsync();
								}).wait();
								delete stm;
								delete file;
							}
						}
						else
						{
							success = false;
							break;
						}
						filenum++;
						this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, filenum, &ugi]() {
							wchar_t buf[64];
							swprintf_s(buf, L"%lu/%lu", filenum, ugi.number_entry);
							pbDownload->Value = (double)filenum;
							tbProgress->Text = ref new Platform::String(buf);
							UpdateLayout();
						}));
					}
					unzClose(uzf);

					if (!success) throw ref new Exception(E_FAIL);
				}).wait();
			}
			catch (Exception^ e)
			{
				ex = e; 
			}

			this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, ex]() {
				Platform::String^ string;
				if (m_Closable)
					string = m_resLdr->GetString("MBDownloadCanceled");
				else if (ex)
					string = String::Concat(m_resLdr->GetString("MBDownloadError"), ex->Message);
				else
					string = m_resLdr->GetString("MBDownloadOK");
				(ref new MessageDialog(string, m_resLdr->GetString("MBDownloadTitle")))->ShowAsync();
				m_Closable = true;
				Hide();
			}));
		});
	}
}


void SDLPal::DownloadDialog::OnDOMContentLoaded(Windows::UI::Xaml::Controls::WebView^ sender, Windows::UI::Xaml::Controls::WebViewDOMContentLoadedEventArgs^ args)
{
	this->Title = sender->DocumentTitle;
	sender->InvokeScriptAsync(ref new String(L"eval"), ref new Vector<String^>(1, ref new String(LR"rs(
	var elems = document.getElementsByTagName('a');
	 for (var i = 0; i < elems.length; i++)
	 {
		if (elems[i].href.indexOf('#') === -1)
		{
			elems[i].target = /\/Pal98rqp\.zip$/i.test(elems[i].href) ? '' : '_blank';
		}
	 }
)rs")));
}
