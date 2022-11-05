#pragma once

_No_competing_thread_
void
BootModulesPreinit(
    void
    );

_No_competing_thread_
STATUS
BootModulesInit(
    IN      PHYSICAL_ADDRESS        BootModulesStart,
    IN      DWORD                   NumberOfModules
    );

_No_competing_thread_
void
BootModulesUninit(
    void
    );

//******************************************************************************
// Function:     BootModuleGet
// Description:  Retrieves the base address and module length for an already
//               mapped module.
// Returns:      STATUS
// Parameter:    IN_Z char * ModuleName - Name of the module
// Parameter:    OUT PVOID * BaseAddress - Base address where the module was
//               mapped.
// Parameter:    OUT QWORD * ModuleLength - Length of the mapped module data.
//******************************************************************************
STATUS
BootModuleGet(
    IN_Z    char*                   ModuleName,
    OUT     PVOID*                  BaseAddress,
    OUT     QWORD*                  ModuleLength
    );
