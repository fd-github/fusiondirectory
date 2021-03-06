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

/*!
 * \file class_templateHandling.inc
 * Source code for the class templateHandling
 */

/*! \brief this class stores static methods used to parse templates LDAP data
 */
class templateHandling
{
  /*! \brief Fetch a template from LDAP and returns its attributes and dependencies information */
  public static function fetch($dn)
  {
    global $config;

    $ldap = $config->get_ldap_link();
    $ldap->cat($dn);
    $attrs    = $ldap->fetch();
    $attrs    = static::fieldsFromLDAP($attrs);
    list($depends, $errors) = static::attributesDependencies($attrs);
    msg_dialog::displayChecks($errors);
    $attrs    = static::sortAttributes($attrs, $depends);
    return array($attrs, $depends);
  }

  /*! \brief Translate template attrs into $attrs as if taken from LDAP */
  public static function fieldsFromLDAP (array $template_attrs)
  {
    $attrs = array();
    if (isset($template_attrs['fdTemplateField'])) {
      unset($template_attrs['fdTemplateField']['count']);
      sort($template_attrs['fdTemplateField']);
      foreach ($template_attrs['fdTemplateField'] as $field) {
        preg_match('/^([^:]+):(.*)$/s', $field, $m);
        if (isset($attrs[$m[1]])) {
          $attrs[$m[1]][] = $m[2];
          $attrs[$m[1]]['count']++;
        } else {
          $attrs[$m[1]]           = array($m[2]);
          $attrs[$m[1]]['count']  = 1;
        }
      }
    }
    return $attrs;
  }

  /*! \brief Translate $attrs into template attrs */
  public static function fieldsToLDAP (array $template_attrs, array $attrs)
  {
    /* First a bit of cleanup */
    unset($template_attrs['dn']);
    unset($template_attrs['fdTemplateField']['count']);
    unset($template_attrs['objectClass']['count']);
    unset($template_attrs['cn']['count']);
    if (isset($template_attrs['count'])) {
      for ($i = 0; $i < $template_attrs['count']; ++$i) {
        /* Remove numeric keys */
        unset($template_attrs[$i]);
      }
    }
    unset($template_attrs['count']);

    /* Remove all concerned values */
    foreach ($template_attrs['fdTemplateField'] as $key => $value) {
      preg_match('/^([^:]+):(.*)$/s', $value, $m);
      if (isset($attrs[$m[1]])) {
        unset($template_attrs['fdTemplateField'][$key]);
      }
    }
    /* Then insert non-empty values */
    foreach ($attrs as $key => $value) {
      if (is_array($value)) {
        foreach ($value as $v) {
          if ($value == "") {
            continue;
          }
          $template_attrs['fdTemplateField'][] = $key.':'.$v;
        }
      } else {
        if ($value == "") {
          continue;
        }
        $template_attrs['fdTemplateField'][] = $key.':'.$value;
      }
    }
    sort($template_attrs['fdTemplateField']);
    return $template_attrs;
  }

  /*! \brief Check template fields
   *
   * Returns errors if there are recursive dependencies.
   * Might check more things later
   */
  public static function checkFields ($attrs)
  {
    list(, $errors) = static::attributesDependencies($attrs);
    return $errors;
  }

  /*! \brief Parse a mask (without surrounding %) using $attrs attributes, apply modifiers and returns an array containing possible results */
  public static function parseMask($mask, array $attrs)
  {
    if ($mask == '|') {
      return array('%');
    }
    $modifiers = '';
    if (preg_match('/^([^|]+)\|/', $mask, $m)) {
      $modifiers = $m[1];
      $mask = substr($mask, strlen($m[0]));
    }
    $result = array('');
    if (isset($attrs[$mask])) {
      $result = array($attrs[$mask]);
      if (is_array($result[0])) {
        unset($result[0]['count']);
      }
    } elseif (($mask != '') && !preg_match('/c/', $modifiers)) {
      trigger_error("'$mask' was not found in attributes");
    }
    $len    = strlen($modifiers);
    for ($i = 0; $i < $len; ++$i) {
      $args     = array();
      $modifier = $modifiers[$i];
      if (preg_match('/^\[([^\]]+)\].*$/', substr($modifiers, $i + 1), $m)) {
        /* get modifier args */
        $args = explode(',', $m[1]);
        $i += strlen($m[1]) + 2;
      }
      $result_tmp = array();
      foreach ($result as $r) {
        $result_tmp = array_merge($result_tmp, static::applyModifier($modifier, $args, $r));
      }
      $result = $result_tmp;
    }
    foreach ($result as &$r) {
      /* Array that were not converted by a modifier into a string are now converted to strings */
      if (is_array($r)) {
        $r = reset($r);
      }
    }
    unset($r);
    return $result;
  }

  /*! \brief Return attrs needed before applying template
   *
   * \return array An array of attributes which are needed by the template
   */
  public static function neededAttrs(array &$attrs, array $flatdepends)
  {
    $needed = array();
    foreach ($flatdepends as $attr => $depends) {
      if ((isset($depends[0])) && ($depends[0] == 'askme')) {
        $needed[] = $attr;
        unset($flatdepends[$attr]);
        unset($attrs[$attr]);
      }
    }
    $dependencies = array_unique(array_merge(...array_values($flatdepends)));
    foreach ($dependencies as $attr) {
      if (empty($flatdepends[$attr])) {
        $needed[] = $attr;
      }
    }
    return array_unique($needed);
  }

  /*! \brief Parse template masks in an array
   *
   * \return array An array with the final values of attributes
   */
  public static function parseArray(array $attrs, array $specialAttrs)
  {
    foreach ($attrs as &$attr) {
      if (is_array($attr)) {
        foreach ($attr as $key => &$string) {
          if (!is_numeric($key)) {
            continue;
          }
          $string = static::parseString($string, array_merge($attrs, $specialAttrs));
        }
        unset($string);
      }
    }
    unset($attr);
    return $attrs;
  }

  /*! \brief Parse template masks in a single string
   *
   * \return string the string with patterns replaced by their values
   */
  public static function parseString($string, array $attrs, $escapeMethod = NULL)
  {
    if (preg_match('/^%%/', $string)) {
      /* Special case: %% at beginning of string means do not touch it. Used by binary attributes. */
      return preg_replace('/^%%/', '', $string);
    }
    $offset = 0;
    while (preg_match('/%([^%]+)%/', $string, $m, PREG_OFFSET_CAPTURE, $offset)) {
      $replace  = static::parseMask($m[1][0], $attrs);
      $replace  = $replace[0];
      if ($escapeMethod !== NULL) {
        $replace = $escapeMethod($replace);
      }
      $string   = substr_replace($string, $replace, $m[0][1], strlen($m[0][0]));
      $offset   = $m[0][1] + strlen($replace);
    }
    return $string;
  }

  /*! \brief Parse template masks in a single string and list the fields it needs
   *
   * \return array An array with the names of the fields used in the string pattern
   */
  public static function listFields($string)
  {
    $fields = array();
    $offset = 0;
    while (preg_match('/%([^%]+)%/', $string, $m, PREG_OFFSET_CAPTURE, $offset)) {
      $mask   = $m[1][0];
      $offset = $m[0][1] + strlen($m[0][0]);
      if ($mask == '|') {
        continue;
      }
      if (preg_match('/^([^|]+)\|/', $mask, $m)) {
        $mask = substr($mask, strlen($m[0]));
      }
      $fields[] = $mask;
    }
    return $fields;
  }

  private static function modifierRemoveAccents($str)
  {
    $str = htmlentities($str, ENT_NOQUOTES, 'UTF-8');

    $str = preg_replace('#&([A-za-z])(?:acute|cedil|circ|grave|orn|ring|slash|th|tilde|uml);#', '\1', $str);
    // handle ligatures
    $str = preg_replace('#&([A-za-z]{2})(?:lig);#', '\1', $str);
    // delete unhandled characters
    return array(preg_replace('#&[^;]+;#', '', $str));
  }

  private static function modifierTranslit(array $args, $str)
  {
    $localesaved = setlocale(LC_CTYPE, 0);
    $ret = array();
    foreach ($args as $arg) {
      setlocale(LC_CTYPE, array($arg,"$arg.UTF8"));
      $ret[] = iconv('UTF8', 'ASCII//TRANSLIT', $str);
    }
    setlocale(LC_CTYPE, $localesaved);
    return array_unique($ret);
  }

  private static function modifierPregReplace(array $args, $str)
  {
    $pattern = '/\s/';
    $replace = '';
    if (count($args) >= 1) {
      $pattern = $args[0];
      if (count($args) >= 2) {
        $replace = $args[1];
      }
    }

    return array(preg_replace($pattern.'u', $replace, $str));
  }

  private static function modifierSubString(array $args, $str)
  {
    if (count($args) < 1) {
      trigger_error("Missing 's' substr modifier parameter");
    }
    if (count($args) < 2) {
      array_unshift($args, 0);
    }
    if (preg_match('/^(\d+)-(\d+)$/', $args[1], $m)) {
      $res = array();
      for ($i = $m[1];$i < $m[2]; ++$i) {
        $res[] = mb_substr($str, $args[0], $i);
      }
      return array_unique($res);
    } else {
      return array(mb_substr($str, $args[0], $args[1]));
    }
  }

  private static function modifierRandomString(array $args)
  {
    $length = 8;
    $chars  = 'b';
    if (count($args) >= 2) {
      $length = random_int($args[0], $args[1]);
      if (count($args) >= 3) {
        $chars = $args[2];
      }
    } elseif (count($args) >= 1) {
      $length = $args[0];
    }
    $res = '';
    for ($i = 0; $i < $length; ++$i) {
      switch ($chars) {
        case 'd':
          /* digits */
          $res .= (string)random_int(0, 9);
          break;
        case 'l':
          /* letters */
          $nb = random_int(65, 116);
          if ($nb > 90) {
            /* lowercase */
            $nb += 6;
          }
          $res .= chr($nb);
          break;
        case 'b':
          /* both */
        default:
          $nb = random_int(65, 126);
          if ($nb > 116) {
            /* digit */
            $nb = (string)($nb - 117);
          } else {
            if ($nb > 90) {
              /* lowercase */
              $nb += 6;
            }
            $nb = chr($nb);
          }
          $res .= $nb;
          break;
      }
    }

    return $res;
  }

  private static function modifierDate(array $args)
  {
    if (count($args) < 1) {
      $args[] = 'now';
    }
    if (count($args) < 2) {
      $args[] = 'd.m.Y';
    }
    $dateObject = new DateTime($args[0], new DateTimeZone('UTC'));
    if ($args[1] == 'epoch') {
      /* Special handling for shadowExpire: days since epoch */
      return array(floor($dateObject->format('U') / 86400));
    }
    return array($dateObject->format($args[1]));
  }

  /*! \brief Apply a modifier
   *
   * \param string $m the modifier
   * \param array $args the parameters
   * \param mixed $str the string or array to apply the modifier on
   *
   * \return array an array of possible values
   * */
  protected static function applyModifier($m, array $args, $str)
  {
    mb_internal_encoding('UTF-8');
    mb_regex_encoding('UTF-8');
    if (is_array($str) && (!is_numeric($m)) && (strtolower($m) == $m)) {
      /* $str is an array and $m is lowercase, so it's a string modifier */
      $str = reset($str);
    }
    $result = array($str);
    switch ($m) {
      case 'F':
        // First
        $result = array(reset($str));
        break;
      case 'L':
        // Last
        $result = array(end($str));
        break;
      case 'J':
        // Join
        if (isset($args[0])) {
          $result = array(join($args[0], $str));
        } else {
          $result = array(join($str));
        }
        break;
      case 'C':
        // Count
        $result = array(count($str));
        break;
      case 'M':
        // Match
        if (count($args) < 1) {
          trigger_error('Missing "M" match modifier parameter');
          $args[] = '/.*/';
        }
        $result = array(array_filter(
          $str,
          function ($s) use ($args)
          {
            return preg_match($args[0], $s);
          }
        ));
        break;
      case '4':
        // IPv4
        $result = array(array_filter($str, 'tests::is_ipv4'));
        break;
      case '6':
        // IPv6
        $result = array(array_filter($str, 'tests::is_ipv6'));
        break;
      case 'c':
        // comment
        $result = array('');
        break;
      case 'b':
        // base64
        if (isset($args[0]) && ($args[0] == 'd')) {
          $result = array(base64_decode($str));
        }
        $result = array(base64_encode($str));
        break;
      case 'u':
        // uppercase
        $result = array(mb_strtoupper($str, 'UTF-8'));
        break;
      case 'l':
        // lowercase
        $result = array(mb_strtolower($str, 'UTF-8'));
        break;
      case 'a':
        // remove accent
        $result = static::modifierRemoveAccents($str);
        break;
      case 't':
        // translit
        $result = static::modifierTranslit($args, $str);
        break;
      case 'p':
        // spaces
        $result = static::modifierPregReplace($args, $str);
        break;
      case 's':
        // substring
        $result = static::modifierSubString($args, $str);
        break;
      case 'r':
        // random string
        $result = array(static::modifierRandomString($args));
        break;
      case 'd':
        // date
        $result = array(static::modifierDate($args));
        break;
      default:
        trigger_error("Unkown modifier '$m'");
        $result = array($str);
        break;
    }
    return $result;
  }

  /*! \brief Flattens dependencies (if a depends of b which depends of c then a depends of c) */
  protected static function flatDepends (&$cache, &$errors, $depends, $key, array $forbidden = array())
  {
    if (isset($cache[$key])) {
      return $cache[$key];
    }

    $forbidden[] = $key;

    $array =
      array_map(
        function ($a) use (&$cache, &$errors, $depends, $forbidden, $key)
        {
          if (in_array($a, $forbidden)) {
            $errors[] = sprintf(
              _('Recursive dependency in the template fields: "%1$s" cannot depend on "%2$s" as "%2$s" already depends on "%1$s"'),
              $key,
              $a
            );
            return array();
          }
          $deps = static::flatDepends ($cache, $errors, $depends, $a, $forbidden);
          if (($askmeKey = array_search('askme', $deps)) !== FALSE) {
            /* Do not flat special askme dependency */
            unset($deps[$askmeKey]);
          }
          return $deps;
        },
        $depends[$key]
      );
    $array[]      = $depends[$key];
    $cache[$key]  = array_unique(array_merge_recursive(...$array));
    return $cache[$key];
  }

  /*! \brief Computes dependencies between attributes: which attributes must be filled in order to compute each attribute value */
  protected static function attributesDependencies(array $attrs)
  {
    /* Compute dependencies of each attr */
    $depends = array();
    foreach ($attrs as $key => $values) {
      $depends[$key] = array();
      if (!is_array($values))  {
        $values = array($values);
      }
      unset ($values['count']);
      foreach ($values as $value) {
        $offset = 0;
        while (preg_match('/%([^%\|]+\|)?([^%]+)%/', $value, $m, PREG_OFFSET_CAPTURE, $offset)) {
          $offset = $m[0][1] + strlen($m[0][0]);
          $depends[$key][] = $m[2][0];
          if (!isset($attrs[$m[2][0]])) {
            /* Dependency which has no value might be missing */
            $attrs[$m[2][0]]    = array();
            $depends[$m[2][0]]  = array();
          }
        }
      }
    }
    /* Flattens dependencies */
    $flatdepends  = array();
    $errors       = array();
    foreach ($depends as $key => $value) {
      static::flatDepends($flatdepends, $errors, $depends, $key);
    }
    return array($flatdepends, $errors);
  }

  /*! \brief Sort attrs depending of dependencies */
  protected static function sortAttributes(array $attrs, array $flatdepends)
  {
    uksort($attrs, function ($k1, $k2) use ($flatdepends) {
      if (in_array($k1, $flatdepends[$k2])) {
        return -1;
      } elseif (in_array($k2, $flatdepends[$k1])) {
        return 1;
      } else {
        /* When no direct dependency, we sort by number of dependencies */
        $c1 = count($flatdepends[$k1]);
        $c2 = count($flatdepends[$k2]);
        if ($c1 == $c2) {
            return 0;
        }
        return (($c1 < $c2) ? -1 : 1);
      }
    });
    return $attrs;
  }
}
