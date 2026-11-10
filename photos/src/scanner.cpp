#include "scanner.h"

#include <f32file.h>
#include <s32file.h>

#include "exif.h"
#include "library.h"
#include "moves.h"
#include "mp4.h"
#include "trace.h"

namespace
    {
    const TInt KMaxDepth = 10;
    const TInt KJpegHeaderBytes = 64 * 1024;
    const TInt KOtherHeaderBytes = 64;
    const TInt KMinSide = 160;
    const TInt KMinBytes = 2048;
    const TInt KMaxBoxes = 64;
    const TInt KMoovBytes = 128 * 1024;

    _LIT(KFoldersSuffix, ".folders");
    const TInt32 KFoldersMagic = 0x50484644;
    const TInt32 KFoldersVersion = 1;
    const TInt KFullWalkHours = 24;
    const TInt KRecentDays = 90;
    }

class CScanner::TShared
    {
public:
    TWorkerLink iLink;

    TFileName iIndexFile;
    const CLibrary* iOld;

    CLibrary* iResult;
    TBool iFinal;
    };

namespace
    {
    class TFound
        {
    public:
        HBufC* iPath;
        TInt iSize;
        TInt64 iModified;
        };

    class TFolder
        {
    public:
        HBufC* iPath;
        TInt64 iModified;
        TBool iRecent;
        };

    class TInFolder
        {
    public:
        const TDesC* iPath;
        TInt iFolderLength;
        TInt iIndex;
        };

    TInt CompareFolders(const TFolder& aA, const TFolder& aB)
        {
        return aA.iPath->CompareF(*aB.iPath);
        }

    TInt CompareInFolder(const TInFolder& aA, const TInFolder& aB)
        {
        return aA.iPath->Left(aA.iFolderLength).CompareF(aB.iPath->Left(aB.iFolderLength));
        }

    class TScan
        {
    public:
        TScan(CScanner::TShared& aShared, RFs& aFs)
            : iShared(aShared), iFs(aFs), iBuffer(NULL), iDirs(0), iParsed(0), iFull(ETrue), iLastFull(0),
              iRead(0), iReused(0) {}
        ~TScan()
            {
            for (TInt i = 0; i < iFound.Count(); i++)
                delete iFound[i].iPath;
            iFound.Close();
            Reset(iOldFolders);
            Reset(iFolders);
            iInFolder.Close();
            delete iBuffer;
            }

        static void Reset(RArray<TFolder>& aFolders)
            {
            for (TInt i = 0; i < aFolders.Count(); i++)
                delete aFolders[i].iPath;
            aFolders.Close();
            }

        TBool Quit() const { return iShared.iLink.iQuit; }

        static TBool IsMedia(const TDesC& aName)
            {
            TParsePtrC parse(aName);
            TPtrC ext = parse.Ext();
            return ext.CompareF(_L(".jpg")) == 0 || ext.CompareF(_L(".jpeg")) == 0 || ext.CompareF(_L(".png")) == 0 ||
                   ext.CompareF(_L(".gif")) == 0 || ext.CompareF(_L(".bmp")) == 0 || CLibrary::IsVideoPath(aName);
            }

        static TBool SkipFolder(const TDesC& aName, TInt aDepth)
            {
            if (aName.Length() == 0 || aName[0] == '.' || aName[0] == '_')
                return ETrue;
            if (aDepth == 0 && (aName.CompareF(_L("private")) == 0 || aName.CompareF(_L("sys")) == 0 ||
                                aName.CompareF(_L("resource")) == 0 || aName.CompareF(_L("system")) == 0))
                return ETrue;
            return aName.CompareF(_L("cache")) == 0 || aName.CompareF(_L("temp")) == 0;
            }

        TInt OldFolder(const TDesC& aPath) const
            {
            TInt lo = 0, hi = iOldFolders.Count();
            while (lo < hi)
                {
                const TInt mid = (lo + hi) / 2;
                const TInt order = iOldFolders[mid].iPath->CompareF(aPath);
                if (order == 0)
                    return mid;
                if (order < 0)
                    lo = mid + 1;
                else
                    hi = mid;
                }
            return -1;
            }

        TInt64 FolderModified(const TDesC& aDir)
            {
            if (aDir.Length() <= 3)
                return 0;
            TTime modified;
            return iFs.Modified(aDir.Left(aDir.Length() - 1), modified) == KErrNone ? modified.Int64() : 0;
            }

        void ReuseL(const TDesC& aDir, TInt aDepth, TInt aOld)
            {
            iReused++;
            const CLibrary& old = *iShared.iOld;
            TInFolder key;
            key.iPath = &aDir;
            key.iFolderLength = aDir.Length();
            key.iIndex = -1;
            TInt at = iInFolder.SpecificFindInOrder(key, TLinearOrder<TInFolder>(CompareInFolder),
                                                    EArrayFindMode_First);
            for (; at >= 0 && at < iInFolder.Count() && CompareInFolder(iInFolder[at], key) == 0; at++)
                {
                const TPhoto& photo = old.Photo(iInFolder[at].iIndex);
                TFound found;
                found.iPath = photo.iPath->AllocLC();
                found.iSize = photo.iSize;
                found.iModified = photo.iModified;
                iFound.AppendL(found);
                CleanupStack::Pop(found.iPath);
                }

            for (TInt i = aOld + 1; i < iOldFolders.Count() && !Quit(); i++)
                {
                const TDesC& path = *iOldFolders[i].iPath;
                if (path.Length() <= aDir.Length() || path.Left(aDir.Length()).CompareF(aDir) != 0)
                    break;
                if (path.Mid(aDir.Length(), path.Length() - aDir.Length() - 1).Locate('\\') == KErrNotFound)
                    {
                    const TFileName child(path);
                    WalkL(child, aDepth + 1);
                    }
                }
            }

        void WalkL(const TDesC& aDir, TInt aDepth)
            {
            if (aDepth > KMaxDepth || Quit())
                return;
            iDirs++;

            const TInt64 modified = FolderModified(aDir);
            const TInt old = iFull || !iShared.iOld ? -1 : OldFolder(aDir);
            TFolder folder;
            folder.iPath = aDir.AllocLC();
            folder.iModified = modified;
            folder.iRecent = EFalse;
            iFolders.AppendL(folder);
            CleanupStack::Pop(folder.iPath);
            if (old >= 0 && aDepth > 1 && modified != 0 && iOldFolders[old].iModified == modified &&
                !iOldFolders[old].iRecent)
                {
                ReuseL(aDir, aDepth, old);
                return;
                }
            iRead++;
            TFileName pattern(aDir);
            pattern.Append('*');
            CDir* entries = NULL;

            if (iFs.GetDir(pattern, KEntryAttNormal | KEntryAttDir, ESortNone, entries) != KErrNone || !entries)
                return;
            CleanupStack::PushL(entries);
            for (TInt i = 0; i < entries->Count(); i++)
                {
                const TEntry& entry = (*entries)[i];
                if (aDir.Length() + entry.iName.Length() + 1 > KMaxFileName)
                    continue;
                TFileName path(aDir);
                path.Append(entry.iName);
                if (entry.IsDir())
                    {
                    if (SkipFolder(entry.iName, aDepth))
                        continue;
                    path.Append('\\');
                    WalkL(path, aDepth + 1);
                    }
                else if (IsMedia(entry.iName) && entry.iSize >= KMinBytes)
                    {
                    TFound found;
                    found.iPath = path.AllocLC();
                    found.iSize = entry.iSize;
                    found.iModified = entry.iModified.Int64();
                    iFound.AppendL(found);
                    CleanupStack::Pop(found.iPath);
                    }
                }
            CleanupStack::PopAndDestroy(entries);
            }

        void WalkDrivesL()
            {
            TDriveList drives;
            User::LeaveIfError(iFs.DriveList(drives));
            for (TInt d = 0; d < KMaxDrives && !Quit(); d++)
                {
                if (!drives[d])
                    continue;
                TDriveInfo info;
                if (iFs.Drive(info, d) != KErrNone || info.iType == EMediaNotPresent || info.iType == EMediaRom ||
                    info.iType == EMediaRam || (info.iDriveAtt & (KDriveAttRom | KDriveAttRemote | KDriveAttSubsted)))
                    continue;
                TChar letter;
                if (RFs::DriveToChar(d, letter) != KErrNone)
                    continue;
                TBuf<16> root;
                root.Append(letter);

                root.Append(d == EDriveC ? _L(":\\Data\\") : _L(":\\"));
                const TInt before = iFound.Count();
                const TUint32 start = User::FastCounter();
                WalkL(root, 0);
                iShared.iLink.Log(_L("scan: %S %d pictures in %d ms"), &root, iFound.Count() - before,
                                  PerfCounterUs(start, User::FastCounter()) / 1000);

                TFileName trash;
                CMoves::TrashFolder(root, trash);
                WalkL(trash, 1);
                }
            }

        void ParseVideo(RFile& aFile, TPhoto& aPhoto)
            {
            TInt fileSize = 0;
            if (aFile.Size(fileSize) != KErrNone)
                return;
            TBuf8<16> head;
            TInt64 at = 0;
            for (TInt box = 0; box < KMaxBoxes && at + 8 <= fileSize; box++)
                {
                if (aFile.Read((TInt)at, head, 16) != KErrNone)
                    return;
                TUint32 type;
                TInt64 size;
                TInt header;
                if (!Mp4BoxHeader(head.Ptr(), head.Length(), type, size, header))
                    return;
                if (type == KMp4Moov)
                    {
                    HBufC8* moov = HBufC8::New(KMoovBytes);
                    if (!moov)
                        return;
                    TPtr8 data = moov->Des();
                    const TInt64 left = (size == 0 ? fileSize - at : size) - header;
                    TVideoHeader video;
                    if (aFile.Read((TInt)(at + header), data, (TInt)Min((TInt64)KMoovBytes, left)) == KErrNone &&
                        ParseMoov(data.Ptr(), data.Length(), video))
                        {
                        aPhoto.iWidth = video.iWidth;
                        aPhoto.iHeight = video.iHeight;
                        aPhoto.iDuration = (video.iDurationMs + 500) / 1000;

                        TTime made(TDateTime(1904, EJanuary, 0, 0, 0, 0, 0));
                        made += TTimeIntervalMicroSeconds(video.iCreated * 1000000);
                        made += User::UTCOffset();
                        if (made.DateTime().Year() >= 2000)
                            aPhoto.iTaken = made.Int64();
                        }
                    delete moov;
                    return;
                    }
                if (size == 0)
                    return;
                at += size;
                }
            }

        TBool ParseL(const TDesC& aPath, TPhoto& aPhoto)
            {
            if (!iBuffer)
                iBuffer = HBufC8::NewL(KJpegHeaderBytes);
            TPtr8 data = iBuffer->Des();
            TParsePtrC parse(aPath);
            const TBool jpeg = parse.Ext().CompareF(_L(".jpg")) == 0 || parse.Ext().CompareF(_L(".jpeg")) == 0;
            RFile file;
            if (file.Open(iFs, aPath, EFileRead | EFileShareReadersOnly) != KErrNone)
                return EFalse;
            if (CLibrary::IsVideoPath(aPath))
                {
                iParsed++;
                ParseVideo(file, aPhoto);
                file.Close();
                aPhoto.iFlags |= TPhoto::EParsed;
                return ETrue;
                }
            TInt err = file.Read(data, jpeg ? KJpegHeaderBytes : KOtherHeaderBytes);
            file.Close();
            if (err != KErrNone)
                return EFalse;
            iParsed++;
            TImageHeader header;
            if (ParseImageHeader(data.Ptr(), data.Length(), header) == EFormatUnknown)
                return EFalse;
            if (header.iWidth > 0 && header.iHeight > 0 && header.iWidth < KMinSide && header.iHeight < KMinSide)
                return EFalse;
            aPhoto.iWidth = header.iWidth;
            aPhoto.iHeight = header.iHeight;
            aPhoto.iOrientation = header.iOrientation;
            if (header.iHasDate)
                {
                TDateTime when;
                if (when.Set(header.iYear, (TMonth)(header.iMonth - 1), header.iDay - 1, header.iHour,
                             header.iMinute, header.iSecond, 0) == KErrNone)
                    aPhoto.iTaken = TTime(when).Int64();
                }
            aPhoto.iFlags |= TPhoto::EParsed;
            return ETrue;
            }

        CLibrary* BuildL(const CLibrary* aOld, TBool aParse)
            {
            CLibrary* library = CLibrary::NewL();
            CleanupStack::PushL(library);
            const TTimeIntervalSeconds utcOffset = User::UTCOffset();
            TInt nextThumb = aOld ? aOld->iNextThumb : 0;
            TInt generation = aOld ? aOld->iThumbGeneration : 0;

            const TBool pack = aOld && nextThumb > 2 * iFound.Count() + 64;
            if (pack)
                {
                nextThumb = 0;
                generation++;
                }
            TInt reused = 0, parsedBefore = iParsed, dropped = 0;
            for (TInt i = 0; i < iFound.Count() && !Quit(); i++)
                {
                const TFound& found = iFound[i];
                const TInt known = aOld ? aOld->Find(*found.iPath) : KErrNotFound;
                const TPhoto* old = known >= 0 ? &aOld->Photo(known) : NULL;
                const TBool same = old && old->iSize == found.iSize && old->iModified == found.iModified;
                TPhoto photo;
                if (same)
                    photo = *old;
                else
                    {
                    Mem::FillZ(&photo, sizeof(photo));
                    photo.iSize = found.iSize;
                    photo.iModified = found.iModified;
                    photo.iOrientation = 1;
                    TTime local(found.iModified);
                    local += utcOffset;
                    photo.iTaken = local.Int64();
                    photo.iThumb = -1;
                    }
                if (pack || photo.iThumb < 0)
                    photo.iThumb = nextThumb++;
                if (aParse && !(photo.iFlags & TPhoto::EParsed) && !ParseL(*found.iPath, photo))
                    {
                    dropped++;
                    continue;
                    }
                if (same)
                    reused++;
                photo.iPath = found.iPath->AllocL();
                library->AppendL(photo);
                }
            library->iNextThumb = nextThumb;
            library->iThumbGeneration = generation;
            library->FinishL();
            iShared.iLink.Log(_L("scan: built %d (%d known, %d read, %d dropped), %d albums, thumbs gen %d next %d"),
                              library->Count(), reused, iParsed - parsedBefore, dropped, library->AlbumCount(),
                              generation, nextThumb);
            CleanupStack::Pop(library);
            return library;
            }

        void FoldersFile(TFileName& aName) const
            {
            aName.Copy(iShared.iIndexFile.Left(KMaxFileName - 8));
            aName.Append(KFoldersSuffix);
            }

        void LoadFolders(TInt64 aNow)
            {
            iFull = ETrue;
            if (!iShared.iOld || iShared.iOld->Count() == 0)
                return;
            TFileName name;
            FoldersFile(name);
            TRAPD(err, LoadFoldersL(name));
            if (err != KErrNone)
                {
                iShared.iLink.Log(_L("scan: no folders list (%d): a full walk"), err);
                Reset(iOldFolders);
                return;
                }
            iFull = aNow - iLastFull > (TInt64)KFullWalkHours * 3600 * 1000000 || aNow < iLastFull;
            if (iFull)
                return;
            iOldFolders.Sort(TLinearOrder<TFolder>(CompareFolders));

            const CLibrary& old = *iShared.iOld;
            const TInt64 recent = aNow - (TInt64)KRecentDays * 24 * 3600 * 1000000;
            for (TInt i = 0; i < old.Count(); i++)
                {
                const TPhoto& photo = old.Photo(i);
                TInFolder entry;
                entry.iPath = photo.iPath;
                entry.iFolderLength = photo.iPath->LocateReverse('\\') + 1;
                entry.iIndex = i;
                if (entry.iFolderLength <= 0)
                    continue;
                iInFolder.AppendL(entry);
                if (photo.iModified < recent)
                    continue;
                TPtrC folder(photo.iPath->Left(entry.iFolderLength));
                while (folder.Length() > 3)
                    {
                    const TInt at = OldFolder(folder);
                    if (at < 0 || iOldFolders[at].iRecent)
                        break;
                    iOldFolders[at].iRecent = ETrue;
                    folder.Set(folder.Left(folder.Left(folder.Length() - 1).LocateReverse('\\') + 1));
                    }
                }
            iInFolder.Sort(TLinearOrder<TInFolder>(CompareInFolder));
            }

        void LoadFoldersL(const TDesC& aName)
            {
            RFileReadStream in;
            User::LeaveIfError(in.Open(iFs, aName, EFileRead | EFileShareReadersOnly));
            CleanupClosePushL(in);
            if (in.ReadInt32L() != KFoldersMagic || in.ReadInt32L() != KFoldersVersion)
                User::Leave(KErrCorrupt);
            const TInt fullHigh = in.ReadInt32L();
            iLastFull = MAKE_TINT64(fullHigh, in.ReadUint32L());
            const TInt count = in.ReadInt32L();
            if (count < 0 || count > 100000)
                User::Leave(KErrCorrupt);
            for (TInt i = 0; i < count; i++)
                {
                TFolder folder;
                folder.iPath = HBufC::NewLC(in, KMaxFileName);
                const TInt high = in.ReadInt32L();
                folder.iModified = MAKE_TINT64(high, in.ReadUint32L());
                folder.iRecent = EFalse;
                iOldFolders.AppendL(folder);
                CleanupStack::Pop(folder.iPath);
                }
            CleanupStack::PopAndDestroy(&in);
            }

        void SaveFoldersL(TInt64 aNow)
            {
            TFileName name, temp;
            FoldersFile(name);
            temp.Copy(name);
            temp.Append(_L(".new"));
            RFileWriteStream out;
            User::LeaveIfError(out.Replace(iFs, temp, EFileWrite));
            CleanupClosePushL(out);
            const TInt64 lastFull = iFull ? aNow : iLastFull;
            out.WriteInt32L(KFoldersMagic);
            out.WriteInt32L(KFoldersVersion);
            out.WriteInt32L(I64HIGH(lastFull));
            out.WriteUint32L(I64LOW(lastFull));
            out.WriteInt32L(iFolders.Count());
            for (TInt i = 0; i < iFolders.Count(); i++)
                {
                out << *iFolders[i].iPath;
                out.WriteInt32L(I64HIGH(iFolders[i].iModified));
                out.WriteUint32L(I64LOW(iFolders[i].iModified));
                }
            out.CommitL();
            CleanupStack::PopAndDestroy(&out);
            iFs.Delete(name);
            User::LeaveIfError(iFs.Rename(temp, name));
            }

        void Publish(CLibrary* aLibrary, TBool aFinal)
            {
            iShared.iResult = aLibrary;
            iShared.iFinal = aFinal;
            iShared.iLink.JobDone(KErrNone);
            }

        void RunL()
            {
            const TUint32 start = User::FastCounter();
            TTime now;
            now.UniversalTime();
            LoadFolders(now.Int64());
            WalkDrivesL();

            if (Quit())
                User::Leave(KErrCancel);
            const TPtrC mode(iFull ? _L("full") : _L("changes only"));
            iShared.iLink.Log(_L("scan: walked %d folders (%S: %d read, %d as last time), %d pictures in %d ms"),
                              iDirs, &mode, iRead, iReused, iFound.Count(),
                              PerfCounterUs(start, User::FastCounter()) / 1000);
            TRAPD(foldersErr, SaveFoldersL(now.Int64()));
            if (foldersErr != KErrNone)
                iShared.iLink.Log(_L("scan: folders list not saved, %d"), foldersErr);
            const CLibrary* old = iShared.iOld;
            if (!old || old->Count() == 0)
                {

                CLibrary* quick = BuildL(NULL, EFalse);
                Publish(quick, EFalse);
                if (!iShared.iLink.WaitForJob())
                    return;
                old = quick;
                }
            const TUint32 buildStart = User::FastCounter();
            CLibrary* library = BuildL(old, ETrue);
            CleanupStack::PushL(library);
            if (Quit())
                User::Leave(KErrCancel);
            const TUint32 saveStart = User::FastCounter();
            TRAPD(saveErr, library->SaveL(iFs, iShared.iIndexFile));
            iShared.iLink.Log(_L("scan: headers in %d ms; index saved err %d in %d ms"),
                              PerfCounterUs(buildStart, saveStart) / 1000, saveErr,
                              PerfCounterUs(saveStart, User::FastCounter()) / 1000);
            CleanupStack::Pop(library);
            Publish(library, ETrue);
            }

        CScanner::TShared& iShared;
        RFs& iFs;
        RArray<TFound> iFound;
        HBufC8* iBuffer;
        TInt iDirs;
        TInt iParsed;
        TBool iFull;
        TInt64 iLastFull;
        RArray<TFolder> iOldFolders;
        RArray<TFolder> iFolders;
        RArray<TInFolder> iInFolder;
        TInt iRead;
        TInt iReused;
        };

    void ScanBody(TAny* aShared)
        {
        CScanner::TShared& shared = *static_cast<CScanner::TShared*>(aShared);
        if (!shared.iLink.WaitForJob())
            return;
        RFs fs;
        TInt err = fs.Connect();
        if (err == KErrNone)
            {
            TScan* scan = new TScan(shared, fs);
            if (scan)
                {
                TRAP(err, scan->RunL());
                delete scan;
                }
            else
                err = KErrNoMemory;
            fs.Close();
            }
        if (err != KErrNone && !shared.iLink.iQuit)
            {
            shared.iLink.Log(_L("scan: failed %d"), err);
            shared.iResult = NULL;
            shared.iFinal = ETrue;
            shared.iLink.JobDone(err);
            }
        }

    TInt ScanThread(TAny* aShared)
        {
        return RunWorkerThread(ScanBody, aShared);
        }
    }

CScanner* CScanner::NewL(MScanObserver& aObserver, const TDesC& aIndexFile)
    {
    TShared* shared = new (ELeave) TShared;
    shared->iLink.iDone = NULL;
    shared->iLink.iQuit = EFalse;
    shared->iIndexFile.Copy(aIndexFile);
    shared->iOld = NULL;
    shared->iResult = NULL;
    shared->iFinal = EFalse;
    CScanner* self = new CScanner(aObserver, shared);
    if (!self)
        {
        delete shared;
        User::Leave(KErrNoMemory);
        }
    return self;
    }

CScanner::CScanner(MScanObserver& aObserver, TShared* aShared)
    : CWorker(aShared->iLink, ScanThread, aShared, _L("scan")), iObserver(aObserver), iShared(aShared)
    {
    }

CScanner::~CScanner()
    {
    Shutdown();
    delete iShared->iResult;
    delete iShared;
    }

void CScanner::Start(const CLibrary* aOld)
    {
    iShared->iOld = aOld;
    iShared->iResult = NULL;
    TInt err = Submit();
    Trace(_L("scan: start err %d, %d known"), err, aOld ? aOld->Count() : 0);
    }

void CScanner::Continue()
    {
    iShared->iResult = NULL;
    Submit();
    }

void CScanner::RunL()
    {
    WriteLog();
    CLibrary* result = iShared->iResult;
    iShared->iResult = NULL;
    const TBool final = iShared->iFinal || iStatus.Int() != KErrNone;
    Trace(_L("scan: result err %d, %d pictures, final %d"), iStatus.Int(), result ? result->Count() : 0, final);
    if (result || final)
        iObserver.LibraryScanned(result, final);
    }
