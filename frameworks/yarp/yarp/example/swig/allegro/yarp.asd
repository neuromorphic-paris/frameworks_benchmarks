;; Copyright (C) 2006-2019 Istituto Italiano di Tecnologia (IIT)
;; Copyright (C) 2008 Lorenz Mosenlechner
;;
;; This library is free software; you can redistribute it and/or
;; modify it under the terms of the GNU Lesser General Public
;; License as published by the Free Software Foundation; either
;; version 2.1 of the License, or (at your option) any later version.
;;
;; This library is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;; Lesser General Public License for more details.
;;
;; You should have received a copy of the GNU Lesser General Public
;; License along with this library; if not, write to the Free Software
;; Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

;(in-package "LOGGING-DB")

;; *****************************************************************************
;; ** LOGGING-DB                                                              **
;; *****************************************************************************

(asdf:defsystem yarp
  :name "yarp"
  :author "Lorenz M�senlechner <moesenle@cs.tum.edu>"
  :version "1.0"
  :maintainer "Lorenz M�senlechner <moesenle@cs.tum.edu>"
  :licence "LGPL v2.1 or later"
  :description "YARP wrappers"
  :long-description "YARP wrappers"

  :depends-on ()

  :components
    ( (:library-directory "lib/")
      (:module
        "src"
        :default-component-class asdf:lisp-source-file
        :components
          ( (:file "yarp") )) ))