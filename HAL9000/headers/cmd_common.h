#pragma once

// warning C28208: Function was previously defined with a different parameter list
// warning C28250: Inconsistent annotation for Function: the prior instance has trailing unannotated parameters
// warning C28251: Inconsistent annotation for Function: this instance has trailing unannotated parameters.
// warning C28253: Inconsistent annotation for Function
#pragma warning(disable:28208 28250 28251 28253)

typedef
void
(__cdecl FUNC_GenericCommand)(
    IN      QWORD           NumberOfParameters,
    ...
    );

typedef FUNC_GenericCommand*    PFUNC_GenericCommand;
