# Copyright 2006-2007, 2013-2014, 2016-2017 Free Software Foundation,
# Inc.

# This file is part of GNU tar.

# GNU tar is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

# GNU tar is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

1{s,/\*,@comment ,
b
}
2,/.*\*\//{s,\*/,,;s/^/@comment/
b
}
/\/* END \*\//,$d
s/\([{}]\)/@\1/g
s,/\*,&@r{,
s,\*/,}&,
