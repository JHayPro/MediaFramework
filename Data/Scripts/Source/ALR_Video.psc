ScriptName ALR_Video extends Quest
Import MediaLoadScreens

Event OnQuestInit()  ; runs once when the quest starts (perfect place to register)
    Debug.Trace("[ALR_Video] OnQuestInit() - Registering for player load")
    RegisterForRemoteEvent(Game.GetPlayer(), "OnPlayerLoadGame")
    QueueMedia()      ; also run it immediately the first time
EndEvent

Event Actor.OnPlayerLoadGame(Actor akSender)
    Debug.Trace("[ALR_Video] OnPlayerLoadGame() - Save loaded")
    QueueMedia()
EndEvent

Function QueueMedia()
    Bool success = False

    MediaLoadScreens:MediaLoadScreensOptions options = new MediaLoadScreens:MediaLoadScreensOptions
    options.persistentPerInstance = true
    options.persistentCrossInstance = false

    String MediaFolderPath = "ALR_Video"
    Int MediaLoadscreensVersion = 0
    Int Priority = 0

    success = MediaLoadScreens.QueueMediaFileOrFolder(options, MediaFolderPath, MediaLoadscreensVersion, Priority)

    Debug.Trace("[ALR_Video] Queued FOLDER: " + MediaFolderPath)
    If (success)
        Debug.Trace("[ALR_Video] SUCCESS")
    Else
        Debug.Trace("[ALR_Video] FAILED")
    EndIf
EndFunction