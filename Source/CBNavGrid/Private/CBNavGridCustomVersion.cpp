#include "CBNavGridCustomVersion.h"
#include "Serialization/CustomVersion.h"

FGuid const FCBNavGridCustomVersion::GUID{ 0x8E1F44CB, 0x66FA4A32, 0xAA59A8C8, 0xA4FA12CB };

FCustomVersionRegistration GRegisterCBNavGridCustomVersion{ FCBNavGridCustomVersion::GUID, FCBNavGridCustomVersion::LatestVersion, TEXT("CBNavGrid") };
