//
// Scheduler.cpp
// CloudSim
//
// Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//
// round robin solution

#include "Scheduler.hpp"
#include <cassert>
#include <queue>
#include <algorithm>
#include <climits>
#include <map>

static bool migrating = false;
static unsigned active_machines = 4;

class PendingTask {
public:
    TaskId_t task;
    MachineId_t machine;

    PendingTask(TaskId_t _task, MachineId_t _machine)
        : task(_task), machine(_machine) {}
};

class MachineAndVms {
public:
    MachineId_t machine;
    VMId_t vms[4];

    MachineAndVms(MachineId_t _machine, VMId_t vm)
        : machine(_machine)
    {
        for (int i = 0; i < 4; i++) {
            vms[i] = INT_MAX;
        }
        if (vm != INT_MAX) {
            vms[VM_GetInfo(vm).vm_type] = vm;
        }
    }
};

static map<TaskId_t, MachineId_t> task_to_machine;
static std::vector<PendingTask*> pending_tasks;
static std::vector<MachineId_t> pendingMachines;
static std::vector<MachineId_t> armMachines;
static std::vector<MachineId_t> powerMachines;
static std::vector<MachineId_t> riscvMachines;
static std::vector<MachineId_t> x86Machines;
static map<MachineId_t, MachineAndVms*> activeMachinesAndVms;

unsigned int nextArmMachine = 0;
unsigned int nextPowerMachine = 0;
unsigned int nextx86Machine = 0;
unsigned int nextRiscVMachine = 0;

static const VMId_t NO_VM = INT_MAX;

// ------------------------------------------------------------
// Safe get-or-create MachineAndVms
// ------------------------------------------------------------
static MachineAndVms* getOrCreateMachineInfo(MachineId_t m) {
    auto it = activeMachinesAndVms.find(m);
    if (it != activeMachinesAndVms.end() && it->second != nullptr)
        return it->second;

    MachineAndVms* mi = new MachineAndVms(m, NO_VM);
    activeMachinesAndVms[m] = mi;
    return mi;
}

// ------------------------------------------------------------
// Pick least-loaded machine
// ------------------------------------------------------------
static MachineId_t pickLeastLoadedMachine(const std::vector<MachineId_t>& vec) {
    assert(!vec.empty());
    MachineId_t best = vec[0];
    unsigned bestMem = Machine_GetInfo(best).memory_used;

    for (MachineId_t m : vec) {
        auto info = Machine_GetInfo(m);
        if (info.memory_used < bestMem) {
            best = m;
            bestMem = info.memory_used;
        }
    }
    return best;
}

// ------------------------------------------------------------
// Scheduler Init
// ------------------------------------------------------------
void Scheduler::Init() {
    SimOutput("Scheduler::Init(): Total number of machines is " +
              to_string(Machine_GetTotal()), 3);
    SimOutput("Scheduler::Init(): Initializing scheduler", 1);

    for (unsigned int i = 0; i < Machine_GetTotal(); i++) {
        machines.push_back(MachineId_t(i));
        CPUType_t t = Machine_GetCPUType(MachineId_t(i));

        if (t == ARM) armMachines.push_back(i);
        else if (t == POWER) powerMachines.push_back(i);
        else if (t == RISCV) riscvMachines.push_back(i);
        else if (t == X86) x86Machines.push_back(i);
    }

    // Attach initial VMs
    for (unsigned int i = 0; i < armMachines.size() / 8; i++) {
        VMId_t vm = VM_Create(LINUX, ARM);
        VM_Attach(vm, armMachines[i]);
        vms.push_back(vm);
        activeMachinesAndVms[armMachines[i]] =
            new MachineAndVms(armMachines[i], vm);
    }

    for (unsigned int i = 0; i < powerMachines.size() / 8; i++) {
        VMId_t vm = VM_Create(LINUX, POWER);
        VM_Attach(vm, powerMachines[i]);
        vms.push_back(vm);
        activeMachinesAndVms[powerMachines[i]] =
            new MachineAndVms(powerMachines[i], vm);
    }

    for (unsigned int i = 0; i < riscvMachines.size() / 8; i++) {
        VMId_t vm = VM_Create(LINUX, RISCV);
        VM_Attach(vm, riscvMachines[i]);
        vms.push_back(vm);
        activeMachinesAndVms[riscvMachines[i]] =
            new MachineAndVms(riscvMachines[i], vm);
    }

    for (unsigned int i = 0; i < x86Machines.size() / 8; i++) {
        VMId_t vm = VM_Create(LINUX, X86);
        VM_Attach(vm, x86Machines[i]);
        vms.push_back(vm);
        activeMachinesAndVms[x86Machines[i]] =
            new MachineAndVms(x86Machines[i], vm);
    }

    // Put unused machines into S1
    for (unsigned int i = 0; i < Machine_GetTotal(); i++) {
        if (activeMachinesAndVms.find(i) == activeMachinesAndVms.end()) {
            Machine_SetState(i, S1);
        }
    }
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    // nothing implemented yet
}

// ------------------------------------------------------------
// New Task
// ------------------------------------------------------------
void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    CPUType_t required_cpu = RequiredCPUType(task_id);
    VMType_t vm_type = RequiredVMType(task_id);

    Priority_t priority;
    if (GetTaskInfo(task_id).required_sla == SLA0) priority = LOW_PRIORITY;
    else if (GetTaskInfo(task_id).required_sla == SLA1 ||
             GetTaskInfo(task_id).required_sla == SLA2)
        priority = MID_PRIORITY;
    else
        priority = HIGH_PRIORITY;

    MachineId_t nextMachine;

    if (required_cpu == ARM) nextMachine = pickLeastLoadedMachine(armMachines);
    else if (required_cpu == POWER) nextMachine = pickLeastLoadedMachine(powerMachines);
    else if (required_cpu == X86) nextMachine = pickLeastLoadedMachine(x86Machines);
    else nextMachine = pickLeastLoadedMachine(riscvMachines);

    MachineAndVms* nextMachineInfo = nullptr;
    auto it = activeMachinesAndVms.find(nextMachine);
    if (it != activeMachinesAndVms.end()) nextMachineInfo = it->second;

    // Wake sleeping machine
    if (Machine_GetInfo(nextMachine).s_state != S0) {
        if (std::find(pendingMachines.begin(), pendingMachines.end(), nextMachine)
            == pendingMachines.end()) {
            Machine_SetState(nextMachine, S0);
            pendingMachines.push_back(nextMachine);
        }
        if (!nextMachineInfo) {
            nextMachineInfo = new MachineAndVms(nextMachine, NO_VM);
            activeMachinesAndVms[nextMachine] = nextMachineInfo;
        }
        pending_tasks.push_back(new PendingTask(task_id, nextMachine));
        SimOutput("Scheduler::NewTask(): Waking machine "
                  + to_string(nextMachine)
                  + " and pending task "
                  + to_string(task_id), 2);
        return;
    }

    // Machine is awake — check memory
    auto minfo = Machine_GetInfo(nextMachine);
    unsigned task_mem = GetTaskMemory(task_id);

    if (minfo.memory_used + task_mem > minfo.memory_size) {
        if (!nextMachineInfo) {
            nextMachineInfo = new MachineAndVms(nextMachine, NO_VM);
            activeMachinesAndVms[nextMachine] = nextMachineInfo;
        }
        pending_tasks.push_back(new PendingTask(task_id, nextMachine));
        SimOutput("Scheduler::NewTask(): Insufficient memory on machine "
                  + to_string(nextMachine)
                  + " -> pending task "
                  + to_string(task_id), 2);
        return;
    }

    if (!nextMachineInfo) {
        nextMachineInfo = new MachineAndVms(nextMachine, NO_VM);
        activeMachinesAndVms[nextMachine] = nextMachineInfo;
    }

    bool assigned = false;

    if (nextMachineInfo->vms[vm_type] == NO_VM) {
        VMId_t vm = VM_Create(vm_type, required_cpu);
        VM_Attach(vm, nextMachine);
        VM_AddTask(vm, task_id, priority);
        nextMachineInfo->vms[vm_type] = vm;
        task_to_machine[task_id] = nextMachine;
        assigned = true;
    } else {
        VM_AddTask(nextMachineInfo->vms[vm_type], task_id, priority);
        task_to_machine[task_id] = nextMachine;
        assigned = true;
    }

    if (assigned) {
        SimOutput("Scheduler::NewTask(): Task "
                  + to_string(task_id)
                  + " assigned to Machine "
                  + to_string(nextMachine), 1);
    } else {
        SimOutput("Scheduler::NewTask(): Could not assign Task "
                  + to_string(task_id), 0);
        SLAWarning(now, task_id);
    }
}

// ------------------------------------------------------------
// Add pending tasks
// ------------------------------------------------------------
void Scheduler::addPendingTasks(Time_t now, MachineId_t machine) {
    for (auto it = pending_tasks.begin(); it != pending_tasks.end();) {
        PendingTask* p = *it;
        MachineId_t neededMachine = p->machine;
        TaskId_t task = p->task;

        if (neededMachine == machine) {
            Priority_t priority;
            auto sla = GetTaskInfo(task).required_sla;

            if (sla == SLA0) priority = LOW_PRIORITY;
            else if (sla == SLA1 || sla == SLA2) priority = MID_PRIORITY;
            else priority = HIGH_PRIORITY;

            MachineAndVms* nextMachineInfo = activeMachinesAndVms[neededMachine];
            if (nextMachineInfo == nullptr) {
                nextMachineInfo = new MachineAndVms(neededMachine, INT_MAX);
                activeMachinesAndVms[neededMachine] = nextMachineInfo;
            }

            VMType_t type = RequiredVMType(task);

            if (nextMachineInfo->vms[type] == INT_MAX) {
                VMId_t vm = VM_Create(type, RequiredCPUType(task));
                VM_Attach(vm, neededMachine);
                VM_AddTask(vm, task, priority);
                nextMachineInfo->vms[type] = vm;
            } else {
                VM_AddTask(nextMachineInfo->vms[type], task, priority);
            }

            SimOutput("Scheduler::NewTask(): Task "
                      + to_string(task)
                      + " on Machine "
                      + to_string(neededMachine), 1);

            delete p;
            it = pending_tasks.erase(it);
        } else {
            ++it;
        }
    }
}

// ------------------------------------------------------------
// PeriodicCheck
// ------------------------------------------------------------
void Scheduler::PeriodicCheck(Time_t now) {
    // placeholder for student logic
}

// ------------------------------------------------------------
// Shutdown
// ------------------------------------------------------------
void Scheduler::Shutdown(Time_t time) {
    for (auto& vm : vms) {
        VM_Shutdown(vm);
    }
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is "
              + to_string(time), 4);
}

// ------------------------------------------------------------
// TaskComplete
// ------------------------------------------------------------
void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    MachineId_t completedOn = NO_VM;

    auto itmap = task_to_machine.find(task_id);
    if (itmap != task_to_machine.end()) {
        completedOn = itmap->second;
        task_to_machine.erase(itmap);
    }

    SimOutput("Scheduler::TaskComplete(): Task "
              + to_string(task_id)
              + " complete at "
              + to_string(now), 4);

    if (completedOn == NO_VM) {
        for (auto it = pending_tasks.begin(); it != pending_tasks.end();) {
            PendingTask* p = *it;
            MachineId_t m = p->machine;

            auto minfo = Machine_GetInfo(m);
            unsigned mem_needed = GetTaskMemory(p->task);

            if (minfo.s_state == S0 &&
                minfo.memory_used + mem_needed <= minfo.memory_size) {

                MachineAndVms* mi = getOrCreateMachineInfo(m);
                VMType_t type = RequiredVMType(p->task);

                if (mi->vms[type] == NO_VM) {
                    VMId_t vm = VM_Create(type, RequiredCPUType(p->task));
                    VM_Attach(vm, m);
                    mi->vms[type] = vm;
                    VM_AddTask(vm, p->task, MID_PRIORITY);
                } else {
                    VM_AddTask(mi->vms[type], p->task, MID_PRIORITY);
                }

                task_to_machine[p->task] = m;
                delete p;
                it = pending_tasks.erase(it);
            } else {
                ++it;
            }
        }
    } else {
        for (auto it = pending_tasks.begin(); it != pending_tasks.end();) {
            PendingTask* p = *it;
            if (p->machine != completedOn) {
                ++it;
                continue;
            }

            auto minfo = Machine_GetInfo(completedOn);
            unsigned mem_needed = GetTaskMemory(p->task);

            if (minfo.s_state == S0 &&
                minfo.memory_used + mem_needed <= minfo.memory_size) {

                MachineAndVms* mi = getOrCreateMachineInfo(completedOn);
                VMType_t type = RequiredVMType(p->task);

                if (mi->vms[type] == NO_VM) {
                    VMId_t vm = VM_Create(type, RequiredCPUType(p->task));
                    VM_Attach(vm, completedOn);
                    mi->vms[type] = vm;
                    VM_AddTask(vm, p->task, MID_PRIORITY);
                } else {
                    VM_AddTask(mi->vms[type], p->task, MID_PRIORITY);
                }

                task_to_machine[p->task] = completedOn;
                delete p;
                it = pending_tasks.erase(it);
            } else {
                ++it;
            }
        }
    }
}

// ------------------------------------------------------------
// Public Hooks
// ------------------------------------------------------------
static Scheduler Scheduler;

void InitScheduler() {
    SimOutput("InitScheduler(): Initializing scheduler", 4);
    Scheduler.Init();
}

void HandleNewTask(Time_t time, TaskId_t task_id) {
    SimOutput("HandleNewTask(): Received new task "
              + to_string(task_id)
              + " at time "
              + to_string(time), 4);
    Scheduler.NewTask(time, task_id);
}

void HandleTaskCompletion(Time_t time, TaskId_t task_id) {
    SimOutput("HandleTaskCompletion(): Task "
              + to_string(task_id)
              + " completed at time "
              + to_string(time), 4);
    Scheduler.TaskComplete(time, task_id);
}

void MemoryWarning(Time_t time, MachineId_t machine_id) {
    SimOutput("MemoryWarning(): Overflow at "
              + to_string(machine_id)
              + " was detected at time "
              + to_string(time), 0);
}

void MigrationDone(Time_t time, VMId_t vm_id) {
    SimOutput("MigrationDone(): Migration of VM "
              + to_string(vm_id)
              + " was completed at time "
              + to_string(time), 4);
    Scheduler.MigrationComplete(time, vm_id);
    migrating = false;
}

void SchedulerCheck(Time_t time) {
    SimOutput("SchedulerCheck(): SchedulerCheck() called at "
              + to_string(time), 4);
    Scheduler.PeriodicCheck(time);
}

void SimulationComplete(Time_t time) {
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl;
    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in "
         << double(time) / 1000000 << " seconds" << endl;

    SimOutput("SimulationComplete(): Simulation finished at time "
              + to_string(time), 4);
    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    auto it = std::find(pendingMachines.begin(), pendingMachines.end(), machine_id);
    if (it != pendingMachines.end()) {
        pendingMachines.erase(it);
    }

    if (Machine_GetInfo(machine_id).s_state == S0) {
        active_machines = active_machines + 1;
        Scheduler.addPendingTasks(time, machine_id);
    }
}
