# linux-speaker-driver
Linux device driver for the pc-speaker

## Metodo de trabajo
Cuando se comience a trabajar en una fase determinada se leeran los distintos puntos a completar de la guia de la practica. Despues se escribiran como todo-list. Según se vaya completando funcionalidad se ira marcando para que los demas tengan la referencia.

## Step 1

Gestión del hardware del dispositivo

*Status:* TERMINADO

- [x] acceso al hardware con outb, inb
- [x] uso de raw spinlock de linux/i8253.h
- [x] desarrollo funcion play (frequency+spkr_on

## Step 2

Alta y baja del dispositivo

*Status:* TERMINADO

- [x] Skeleton functions for file_operations: open, write and release 
- [x] ON INIT: Automatic creation of /sys/class/speaker/intspkr and /dev/intspkr to access the driver
- [x] ON EXIT: Remove user accesible files

## Step 3

Operaciones de apertura y cierre

*Status:* TERMINADO

- [x ] Write mode: Only allow 1 file open, else: EBUSY (he usado Mutex)
- [x ] Read mode: Allow infinite opens

## Step 4

Operación de escritura

*Status:* TERMINADO

- [x] Muchas cosas que hacer, hechas

## Step 5

Operación fsync y adapaptación a la versión 3.0.X de Linux

*Status:* FUNCIONAL (verificar punto por si diera fallo)

- [x] fsync spinlock
- [ ] verificar que el spinlock de spkr-io para linux 3.0.X no necesita inicializarse (cuando no hay fichero linux/i8253.h

## Step 6

Operaciones ioctl

*Status:*

- [ ] SET_MUTE
- [x] GET_MUTE
- [ ] RESET

## OTHER

- [ ] PDF memoria
- [ ] Verificar que los spinlocks y mutex estan bien puestos en la teoría (_bh en callback, spinlock_bh en fsync, raw_spinlock_irqsave en spkr, mutex para open, mutex para ioctl
- [ ] Seccion en pdf respecto a la concurrencia

