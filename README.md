# PG0 - запуск сервера postgresql прямо с бэкапа
PG0 позволяет запустить сервер postgresql прямо с директории бэкапа, созданной pg\_probackup. Восстановление при этом не требуется.

PG0 использует filesystem in user space (FUSE) чтобы эмулировать всю директорию кластера поверх директории бэкапа.

Root пользователь для монтирования не требуется. Это позволяет разделять роли администратора базы данных и unix администратора.

PG0 _никогда_ не пишет в бэкап. Все изменения базы данных хранятся в оперативной памяти и теряются после размонтирования. Однако, можно использовать overlayfs (как в докере) для сохранения разных уровней модификации базы.

## Для чего можно использовать
* быстрое "восстановление чтобы восстановить пару значений из удалённых данных на какую-то дату" (с помощью FDW или просто pg\_dump)
* покопаться в данных на дату "чтобы выяснить кое-что"
* в критических случаях, когда время восстановления из бэкапа большое, может сразу поддерживать prod в режиме только для чтения (с падением скорости, но "хоть как-то работает")
* дешево откатиться на конкретный момент когда глючило клиентское приложение, чтобы запустить его там для тестирования
* Альтернатива реплике для запуска отчётов. Можно запускать формирование отчётов "за прошлый отчётный период" прямо на бэкапе без использования дополнительного места для восстановления
* при использовании overlayfs можно "поиграть" с базой дёшево сохраняя разные варианты
* pg0 bisect - аналог git bisect
* ...другие идеи, которые становятся возможными из-за нулевой стоимости восстановления

## Плюсы
* работает на любой версии постгреса
* дополнительное место и время для восстановления не требуются
* Весь сервер сразу доступен. Больше свободы - хочешь dump делай, хочешь - отдельные строки или даже джойны или FDW к нему
* Нет сложной последовательности шагов. Любой администратор знает что такое "монтировать" диск

## Недостатки
* скорость работы (но может быть лучше чем восстановление базы, потому что нигде ничего не копируется)
* PG0 и постгрес конкурируют за буферный кеш операционки

## Планы
* проверка граничных условий - дельта, даты, сжатие
* обработка ошибок
* идти от инодов а не от структур данных pg\_probackup
* управление памятью
* Параметры запуска
   - пути к бэкапам
   - точка во времени, или номер транзакции
   - what
   - куда монтировать
   - объём кеширования
* Протестировать с различными сложными сценариями работы постгреса
* переключиться на более быстрый "низкоуровневый" FUSE API
* поддержка многопоточности в FUSE
* mmap
* Поддержка дельт, ptrack и так далее
* Поддержка сжатия
* Табличные пространства
* windows - там есть аналог FUSE с тем же API
* Поддежка конкурирующих форматов (pg\_basebackup, wal-g, bacula, barman, pgBackRest)
* Франкенштейн: Чтобы разные таблицы были видны из разных дампов или из прода (игнорируя консистентность)

## Запуск примера в докере
Посмотреть как работает PG0 можно с помощью демо. Чтобы его запустить нужно сначала собрать образ со всем необходимым:
```
docker build --network host -t pg0 .
```

Затем запустить саму демку:
```
docker run --privileged -it pg0:latest
```

Чтобы лучше понять как работает демка смотрите текст demo.sh. В конце скрипт запускает два идентичных селекта из
исходной базы и из базы которая поднята на маунте FUSE.