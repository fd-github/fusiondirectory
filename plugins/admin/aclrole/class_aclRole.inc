<?php
/*
  This code is part of FusionDirectory (http://www.fusiondirectory.org/)
  Copyright (C) 2011-2016  FusionDirectory

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
*/

/* complicated stuff */
class ACLsAttribute extends DialogOrderedArrayAttribute
{
  protected $order        = TRUE;
  protected $dialogClass  = 'ACLEditionDialog';

  protected function getAttributeArrayValue($key, $value)
  {
    /* Convert text value to displayable array value */

    /* Summarize ACL */
    $summary = '';
    foreach ($value as $name => $object) {
      if (count($object)) {
        $summary .= $name.', ';
      }
    }
    $summary = sprintf(_('Contains settings for these objects: %s'), preg_replace('/, $/', '', $summary));
    return array($summary);
  }

  function readValue($value)
  {
    $acl = explode(':', $value, 2);
    return array($acl[0], acl::extractACL($acl[1]));
  }

  function writeValue($key, $value)
  {
    $acl = '';
    foreach ($value as $object => $contents) {
      /* Only save, if we've some contents in there... */
      if (count($contents)) {
        $acl .= $object.';';
        foreach ($contents as $attr => $permission) {
          /* First entry? Its the one for global settings... */
          if ($attr == '0') {
            $acl .= $permission;
          } else {
            $acl .= '#'.$attr.';'.$permission;
          }
        }
        $acl .= ',';
      }
    }
    return $key.':'.preg_replace('/,$/', '', $acl);
  }
}

class aclRole extends simplePlugin
{
  static function plInfo()
  {
    return array(
      'plShortName'   => _('Role'),
      'plDescription' => _('Access control roles'),
      'plObjectClass' => array('gosaRole'),
      'plObjectType'  => array('aclRole' => array(
        'aclCategory' => 'acl',
        'name'        => _('ACL role'),
        'ou'          => get_ou('aclRoleRDN'),
        'icon'        => 'geticon.php?context=categories&icon=acl&size=16',
      )),

      'plProvidedAcls' => parent::generatePlProvidedAcls(static::getAttributesInfo())
    );
  }

  static function getAttributesInfo()
  {
    return array(
      'properties' => array(
        'name'  => _('Properties'),
        'attrs' => array(
          new BaseSelectorAttribute(get_ou('aclRoleRDN')),
          new HostNameAttribute(
            _('Name'), _('A name for this role'),
            'cn', TRUE
          ),
          new TextAreaAttribute(
            _('Description'), _('Short description of this role'),
            'description', FALSE
          ),
        )
      ),
      'acls' => array(
        'name'  => _('ACLs'),
        'class' => array('fullwidth'),
        'attrs' => array(
          new ACLsAttribute(
            '', _('ACLs which are part of this group'),
            'gosaAclTemplate', TRUE
          ),
        )
      ),
    );
  }
}
?>
