<?php

/*
  This code is part of FusionDirectory (http://www.fusiondirectory.org/)
  Copyright (C) 2003-2010  Cajus Pollmeier
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

/*!
 * \file class_pluglist.inc
 * Source code for the class pluglist
 */

/*!
 * \brief This class contains all the function needed to make list
 * of plugin and manage them
 *
 * \see class_plugin
 */
class pluglist {
  var $menu             = "";
  var $iconmenu         = "";
  var $current          = "";

  /*!
   * \brief The plInfo result for all plugin, using class as key.
   * Contains the plugin index in 'INDEX' and the path in 'PATH'
   */
  var $info             = array();

  /*!
   * \brief Foreign references on DNs
   */
  var $dnForeignRefs = array();

  /*!
   * \brief Using the plugin index as a key, the class of the plugin.
   */
  var $dirlist          = array();

  /*!
   * \brief List plugin indexes of all plugin that the user have acl for
   */
  var $allowed_plugins  = array();
  var $silly_cache      = array();

  /*!
   * \brief List the plugins
   */
  function __construct()
  {
    global $class_mapping;

    /* Fill info part of pluglist */
    $classes = get_declared_classes();
    /* To avoid plugins changing index when reloading */
    sort($classes);

    $index = 0;
    $depends_infos    = array();
    $conflicts_infos  = array();
    $foreign_refs     = array();
    foreach ($classes as $cname) {
      $cmethods = get_class_methods($cname);
      if (in_array_ics('plInfo', $cmethods)) {
        $infos = call_user_func(array($cname, 'plInfo'));
        if (empty($infos)) {
          continue;
        }
        if (is_subclass_of($cname, 'simpleService')) {
          $infos['plSelfModify']  = FALSE;
          /* services are not part of any objectType */
          unset($infos['plObjectType']);
          $infos['plCategory']    = array('server');
        } else {
          if (!isset($infos['plSelfModify'])) {
            $infos['plSelfModify'] = FALSE;
          }
        }
        if (isset($class_mapping[$cname])) {
          $infos['PATH'] = dirname($class_mapping[$cname]);
        }
        if (isset($infos['plDepends'])) {
          $depends_infos[] = $cname;
        }
        if (isset($infos['plConflicts'])) {
          $conflicts_infos[] = $cname;
        }
        if (isset($infos['plForeignKeys'])) {
          foreach ($infos['plForeignKeys'] as $ofield => &$pfks) {
            if (!is_array($pfks)) {
              $pfks = array($pfks);
            }
            if (!is_array($pfks[0])) {
              $pfks = array($pfks);
            }
            foreach ($pfks as &$pfk) {
              $class = $pfk[0];
              if (isset($pfk[1])) {
                $field = $pfk[1];
              } else {
                $field  = 'dn';
                $pfk[1] = $field;
              }
              $filter = NULL;
              if (isset($pfk[2])) {
                $filter = $pfk[2];
              }
              if ($filter === NULL) {
                $filter = "$ofield=%oldvalue%";
              }
              $pfk[2] = $filter;
              if (!isset($foreign_refs[$class])) {
                $foreign_refs[$class] = array();
              }
              if (!isset($foreign_refs[$class][$field])) {
                $foreign_refs[$class][$field] = array();
              }
              $foreign_refs[$class][$field][] = array($cname, $ofield, $filter);
              if ($field == 'dn') {
                $this->dnForeignRefs[] = array($cname, $ofield, $filter, (isset($pfk[3]) ? $pfk[3] : "$ofield=*%oldvalue%"));
              }
            }
            unset($pfk);
          }
          unset($pfks);
        } else {
          $infos['plForeignKeys'] = array();
        }
        if (!isset($infos['plProvidedAcls'])) {
          $infos['plProvidedAcls'] = array();
        }
        if (!isset($infos['plCategory'])) {
          $infos['plCategory'] = array();
        }
        if (!isset($infos['plTitle']) && isset($infos['plShortName'])) {
          $infos['plTitle'] = $infos['plShortName'];
        }
        if (!empty($infos['plObjectClass']) && !isset($infos['plFilter'])) {
          if (count($infos['plObjectClass']) == 1) {
            $infos['plFilter'] = '(objectClass='.$infos['plObjectClass'][0].')';
          } else {
            $infos['plFilter'] = '(&(objectClass='.implode(')(objectClass=', $infos['plObjectClass']).'))';
          }
        }
        if (isset($infos['plFilter'])) {
          $infos['plFilterObject'] = ldapFilter::parse($infos['plFilter']);
        }
        $infos['plForeignRefs']         = array();
        $infos['INDEX']                 = $index;
        $this->info[$cname]             = $infos;
        $this->dirlist[$index++]        = $cname;
      }
    }

    foreach ($depends_infos as $cname) {
      foreach ($this->info[$cname]['plDepends'] as $depend) {
        if (isset($this->info[$depend])) {
          if (isset($this->info[$depend]['plDepending'])) {
            $this->info[$depend]['plDepending'][] = $cname;
          } else {
            $this->info[$depend]['plDepending'] = array($cname);
          }
        } else {
          trigger_error("$cname depends of the inexisting plugin $depend");
        }
      }
    }
    foreach ($conflicts_infos as $cname) {
      foreach ($this->info[$cname]['plConflicts'] as $conflict) {
        if (isset($this->info[$conflict])) {
          if (isset($this->info[$conflict]['plConflicts'])) {
            if (!in_array($cname, $this->info[$conflict]['plConflicts'])) {
              $this->info[$conflict]['plConflicts'][] = $cname;
            }
          } else {
            $this->info[$conflict]['plConflicts'] = array($cname);
          }
        }
      }
    }
    foreach ($foreign_refs as $cname => $refs) {
      if (isset($this->info[$cname])) {
        $this->info[$cname]['plForeignRefs'] = $refs;
      }
    }

    /* Provide field for 'all' */
    $this->info['all'] = array();

    $this->info['all']['plProvidedAcls']  = array();
    $this->info['all']['plDescription']   = _("All objects in this category");
    $this->info['all']['plSelfModify']    = FALSE;

    uasort($this->info,
      function ($a, $b)
      {
        if (isset($a['plPriority']) && isset($b['plPriority'])) {
          if ($a['plPriority'] == $b['plPriority']) {
            return 0;
          } elseif ($a['plPriority'] < $b['plPriority']) {
            return -1;
          } else {
            return 1;
          }
        } elseif (isset($a['plPriority'])) {
          return -1;
        } elseif (isset($b['plPriority'])) {
          return 1;
        } else {
          return 0;
        }
      }
    );
  }

  /*!
   * \brief  Check whether we are allowed to modify the given acl or not
   *
   * This function is used to check which plugins are visible.
   *
   * \param array $infos The acl infos, may contain keys CLASS and/or ACL
   *
   * \return Boolean TRUE on success FALSE otherwise
   */
  function check_access($infos)
  {
    global $ui;

    if (isset($infos['CLASS']) && $ui->isBlacklisted($infos['CLASS'])) {
      return FALSE;
    }

    if (!isset($infos['ACL'])) {
      return TRUE;
    }

    $aclname = $infos['ACL'];

    if (isset($this->silly_cache[$aclname])) {
      return $this->silly_cache[$aclname];
    }

    /* Split given acl string into an array.
      e.g. "user,systems" => array("user","systems");
         */
    $acls_to_check = array();
    if (preg_match("/,/", $aclname)) {
      $acls_to_check = explode(",", $aclname);
    } else {
      $acls_to_check = array($aclname);
    }

    foreach ($acls_to_check as $acl_to_check) {
      $acl_to_check = trim($acl_to_check);

      /* Check if the given acl tag is only valid for self acl entries
                 <plugin acl="user/user:self" class="user"...  */
      if (preg_match("/:self$/", $acl_to_check)) {
        $acl_to_check = preg_replace("/:self$/", "", $acl_to_check);
        if (strpos($acl_to_check, '/')) {
          if ($ui->get_permissions($ui->dn, $acl_to_check, "") != "") {
            $this->silly_cache[$aclname] = TRUE;
            return TRUE;
          }
        } else {
          if ($ui->get_category_permissions($ui->dn, $acl_to_check) != '') {
            $this->silly_cache[$aclname] = TRUE;
            return TRUE;
          }
        }
      } else {

        /* No self acls. Check if we have any acls for the given ACL type */
        $deps = $ui->get_module_departments($acl_to_check, TRUE);
        if (count($deps)) {
          $this->silly_cache[$aclname] = TRUE;
          return TRUE;
        }
      }
    }

    $this->silly_cache[$aclname] = FALSE;
    return FALSE;
  }

  /*!
   * \brief Get headline, description and icon of a plugin
   */
  function get_infos($cname)
  {
    $plHeadline     = FALSE;
    $plIcon         = FALSE;
    $plDescription  = FALSE;
    $index          = $this->get_index($cname);
    $href           = "main.php?plug=$index&amp;reset=1";
    if (isset($this->info[$cname])) {
      if (isset($this->info[$cname]['plShortName'])) {
        $plHeadline = $this->info[$cname]['plShortName'];
      }
      if (isset($this->info[$cname]['plIcon'])) {
        $plIcon = $this->info[$cname]['plIcon'];
      }
      if (isset($this->info[$cname]['plDescription'])) {
        $plDescription = $this->info[$cname]['plDescription'];
      }
      if ($plHeadline && $plIcon && $plDescription) {
        return array($plHeadline,$plDescription,$href,$plIcon);
      }
    }
    $vars = get_class_vars($cname);
    if ($vars) {
      if (!$plHeadline && isset($vars['plHeadline'])) {
        $plHeadline = _($vars['plHeadline']);
      }
      if (!$plDescription && isset($vars['plDescription'])) {
        $plDescription = _($vars['plDescription']);
      }
      if (!$plIcon && isset($vars['plIcon'])) {
        $plIcon = $vars['plIcon'];
      }
    } else {
      die('Unknown class '.$cname);
    }
    if (!$plIcon) {
      $plIcon = "icon.png";
    }
    return array($plHeadline,$plDescription,$href,$plIcon);
  }

  /*!
   * \brief Generate menu
   */
  function gen_menu()
  {
    global $config;
    if ($this->menu == "") {
      $this->menu = '<ul class="menu">'."\n";
      /* Parse headlines */
      foreach ($config->data['SECTIONS'] as $section => $section_infos) {
        $entries  = '';

        /* Parse sub-plugins */
        foreach ($config->data['MENU'][$section] as $info) {
          if (!$this->check_access($info)) {
            continue;
          }
          if (isset($info['CLASS']) && plugin_available($info['CLASS'])) {
            $index  = $this->get_index($info['CLASS']);
            $this->allowed_plugins[$index] = $index;
            list ($plHeadline, $plDescription, $href, ) = $this->get_infos($info['CLASS']);
            $id             = $info['CLASS'];
          } elseif (!isset($info['CLASS'])) {
            $plHeadline     = $info['TITLE'];
            $plDescription  = $info['DESCRIPTION'];
            $href           = $info['LINK'];
            $id             = $info['NAME'];
          } else {
            continue;
          }

          $entries .= '<li class="menuitem" id="menuitem_'.$id.'">';
          $entries .= '<a href="'.$href.'" title="'.$plDescription.'">'.$plHeadline.'</a></li>'."\n";
        }

        /* Append to menu */
        if ($entries != "") {
          $this->menu .= '<li><a>'.$section_infos['NAME']."</a>\n<ul>\n".$entries."\n</ul></li>\n";
        }
      }
      $this->menu .= '</ul>'."\n";
    }

    /* Add the menucurrent class to current plugin */
    if (isset($_GET['plug'])) {
      $plug = $_GET['plug'];
    } else {
      $plug = "NOTHING";
    }
    $lines  = preg_split("/\n/", $this->menu);
    foreach ($lines as &$line) {
      if (preg_match('/'.preg_quote("main.php?plug=$plug&amp;reset=1", '/').'/', $line)) {
        $line = preg_replace('/class="menuitem"/', 'class="menuitem menucurrent"', $line);
      } elseif (preg_match('/class="menuitem menucurrent"/', $line)) {
        $line = preg_replace('/class="menuitem menucurrent"/', 'class="menuitem"', $line);
      }
    }
    unset($line);

    /* Write menu output */
    $this->menu = join("\n", $lines);
  }

  /*!
   * \brief Show the menu icon
   */
  function show_iconmenu()
  {
    global $class_mapping, $config;
    if ($this->iconmenu == "") {

      /* Parse headlines */
      foreach ($config->data['SECTIONS'] as $section => $section_infos) {
        $entries      = '';
        $sectionMenu  = '<div class="iconmenu-section"><h1 class="menuheader">';
        $sectionMenu  .= $section_infos['NAME']."</h1>\n";

        foreach ($config->data['MENU'][$section] as $info) {
          if (!$this->check_access($info)) {
            continue;
          }
          if (isset($info['CLASS']) && plugin_available($info['CLASS'])) {
            /* Read information from class variable */
            list ($plHeadline, $plDescription, $href, $plIcon) = $this->get_infos($info['CLASS']);
            $id             = $info['CLASS'];
          } elseif (!isset($info['CLASS'])) {
            $plHeadline     = $info['TITLE'];
            $plDescription  = $info['DESCRIPTION'];
            $href           = $info['LINK'];
            $plIcon         = $info['ICONPATH'];
            $id             = $info['NAME'];
          } else {
            continue;
          }

          /* Load icon */
          if (isset($info['CLASS']) && !preg_match("/\//", $plIcon) && !preg_match("/^geticon/", $plIcon)) {
            $image = get_template_path("plugins/".preg_replace('%^.*/([^/]+)/[^/]+$%', '\1', $class_mapping[$info['CLASS']])."/images/$plIcon");
          } else {
            $image = htmlentities($plIcon, ENT_COMPAT, 'UTF-8');
          }

          $entries  .= '<div class="iconmenu" id="menuitem_icon_'.$id.'" onClick=\'location.href="'.$href.'"\' title="'.$plDescription.'">';
          $item     = '<div class="imgcontainer"><img src="'.$image.'" alt="'.$plHeadline.' icon"/></div><span>&nbsp;'.$plHeadline.'</span>';
          $entries  .= $item."</div>\n";
        }

        /* Append to menu */
        if ($entries != "") {
          $this->iconmenu .= $sectionMenu.$entries."</div>\n";
        }
      }
    }

    /* Write menu output */
    return $this->iconmenu;
  }

  /*
   * \brief Get the path of the index
   *
   * \param string $index The index which we want the path
   */
  function get_path($index)
  {
    if (!isset($this->dirlist[$index])) {
      return "";
    }
    return "../".$this->info[$this->dirlist[$index]]['PATH'];
  }

  /*
   * \brief Search for plugin index (id), identify entry by path and class
   *
   * \param string $class The name of the class
   */
  function get_index($class)
  {
    /* Search for plugin index (id), identify entry by class */
    if (isset($this->info[$class])) {
      return $this->info[$class]['INDEX'];
    }

    /* Nothing */
    return 0;
  }

  /*!
   * \brief This function checks if we are allowed to view the plugin with the given id
   *
   * \param integer $plug_id  The ID of the plugin.
   *
   * \return Boolean TRUE if we are allowed to view the plugin else FALSE
   */
  function plugin_access_allowed($plug_id)
  {
    return isset($this->allowed_plugins[$plug_id]);
  }

  static function pluginInfos($cname)
  {
    $plist = session::global_get('plist');
    if ($plist) {
      return $plist->info[$cname];
    } else {
      trigger_error('plist not loaded yet');
    }
  }

  /*!
   * \brief Loads plist and load it in config object
   */
  static function load($ldap_available = TRUE)
  {
    global $config, $plist;
    if (!session::global_is_set('plist')) {
      /* Initially load all classes */
      load_all_classes();

      $plist = new pluglist();
      session::global_set('plist', $plist);
      $config->loadPlist($plist);
      if ($ldap_available) {
        $config->get_departments();
        $config->make_idepartments();
      }
    }
    return session::global_get('plist');
  }
}
?>
